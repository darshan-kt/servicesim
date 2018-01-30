/*
 * Copyright (C) 2018 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/

#include <functional>

#include <ignition/math/Pose3.hh>
#include <ignition/math/Vector3.hh>

#include <gazebo/common/Animation.hh>
#include <gazebo/common/Console.hh>
#include <gazebo/common/KeyFrame.hh>
#include <gazebo/physics/physics.hh>

#include <ros/ros.h>
#include <servicesim_competition/DropOffGuest.h>
#include <servicesim_competition/PickUpGuest.h>

#include "FollowActorPlugin.hh"

using namespace gazebo;
using namespace servicesim;
GZ_REGISTER_MODEL_PLUGIN(servicesim::FollowActorPlugin)

class servicesim::FollowActorPluginPrivate
{
  /// \brief Pointer to the actor.
  public: physics::ActorPtr actor{nullptr};

  /// \brief Velocity of the actor
  public: double velocity{0.8};

  /// \brief List of connections such as WorldUpdateBegin
  public: std::vector<event::ConnectionPtr> connections;

  /// \brief Current target model to follow
  public: physics::ModelPtr target{nullptr};

  /// \brief Minimum distance in meters to keep away from target.
  public: double minDistance{1.2};

  /// \brief Maximum distance in meters to keep away from target.
  public: double maxDistance{4};

  /// \brief Radius around actor from where it can be picked up
  public: double pickUpRadius{2};

  /// \brief Margin by which to increase an obstacle's bounding box on every
  /// direction (2x per axis).
  public: double obstacleMargin{0.5};

  /// \brief Time scaling factor. Used to coordinate translational motion
  /// with the actor's walking animation.
  public: double animationFactor{5.1};

  /// \brief Time of the last update.
  public: common::Time lastUpdate;

  /// \brief List of models to ignore when checking collisions.
  public: std::vector<std::string> ignoreModels;

  /// \brief Ros node handle
  public: std::unique_ptr<ros::NodeHandle> rosNode;

  /// \brief PickUp ROS service
  public: ros::ServiceServer pickUpRosService;

  /// \brief DropOff ROS service
  public: ros::ServiceServer dropOffRosService;
};

/////////////////////////////////////////////////
FollowActorPlugin::FollowActorPlugin()
    : dataPtr(new FollowActorPluginPrivate)
{
}

/////////////////////////////////////////////////
void FollowActorPlugin::Load(physics::ModelPtr _model, sdf::ElementPtr _sdf)
{
  this->dataPtr->actor = boost::dynamic_pointer_cast<physics::Actor>(_model);

  // Read in the velocity
  if (_sdf->HasElement("velocity"))
    this->dataPtr->velocity = _sdf->Get<double>("velocity");

  // Read in the follow distance
  if (_sdf->HasElement("min_distance"))
    this->dataPtr->minDistance = _sdf->Get<double>("min_distance");

  // Read in the follow distance
  if (_sdf->HasElement("max_distance"))
    this->dataPtr->maxDistance = _sdf->Get<double>("max_distance");

  // Read in the pickup radius
  if (_sdf->HasElement("pickup_radius"))
    this->dataPtr->pickUpRadius = _sdf->Get<double>("pickup_radius");

  // Read in the obstacle margin
  if (_sdf->HasElement("obstacle_margin"))
    this->dataPtr->obstacleMargin = _sdf->Get<double>("obstacle_margin");

  // Read in the animation factor
  if (_sdf->HasElement("animation_factor"))
    this->dataPtr->animationFactor = _sdf->Get<double>("animation_factor");

  // Add our own name to models we should ignore when avoiding obstacles.
  this->dataPtr->ignoreModels.push_back(this->dataPtr->actor->GetName());

  // Read in the other obstacles to ignore
  if (_sdf->HasElement("ignore_obstacle"))
  {
    auto ignoreElem = _sdf->GetElement("ignore_obstacle");
    while (ignoreElem)
    {
      this->dataPtr->ignoreModels.push_back(ignoreElem->Get<std::string>());
      ignoreElem = ignoreElem->GetNextElement("ignore_obstacle");
    }
  }

  // Read in the animation name
  std::string animation{"animation"};
  if (_sdf->HasElement("animation"))
    animation = _sdf->Get<std::string>("animation");

  auto skelAnims = this->dataPtr->actor->SkeletonAnimations();
  if (skelAnims.find(animation) == skelAnims.end())
  {
    gzerr << "Skeleton animation [" << animation << "] not found in Actor."
          << std::endl;
  }
  else
  {
    // Set custom trajectory
    physics::TrajectoryInfoPtr trajectoryInfo(new physics::TrajectoryInfo());
    trajectoryInfo->type = animation;
    trajectoryInfo->duration = 1.0;

    this->dataPtr->actor->SetCustomTrajectory(trajectoryInfo);
  }


  // Update loop
  this->dataPtr->connections.push_back(event::Events::ConnectWorldUpdateBegin(
      std::bind(&FollowActorPlugin::OnUpdate, this, std::placeholders::_1)));

  // ROS transport
  if (!ros::isInitialized())
  {
    ROS_FATAL_STREAM("A ROS node for Gazebo has not been initialized,"
        << "unable to load plugin. Load the Gazebo system plugin "
        << "'libgazebo_ros_api_plugin.so' in the gazebo_ros package)");
    return;
  }

  this->dataPtr->rosNode.reset(new ros::NodeHandle());

  this->dataPtr->pickUpRosService = this->dataPtr->rosNode->advertiseService(
      "/servicesim/pickup_guest", &FollowActorPlugin::OnPickUpRosRequest, this);

  this->dataPtr->dropOffRosService = this->dataPtr->rosNode->advertiseService(
      "/servicesim/dropoff_guest", &FollowActorPlugin::OnDropOffRosRequest,
      this);
}

/////////////////////////////////////////////////
void FollowActorPlugin::Reset()
{
  this->dataPtr->target = nullptr;
  this->dataPtr->lastUpdate = common::Time::Zero;
}

/////////////////////////////////////////////////
bool FollowActorPlugin::ObstacleOnTheWay() const
{
  auto actorPose = this->dataPtr->actor->WorldPose().Pos();
  auto world = this->dataPtr->actor->GetWorld();

  // Iterate over all models in the world
  for (unsigned int i = 0; i < world->ModelCount(); ++i)
  {
    // Skip models we're ignoring
    auto model = world->ModelByIndex(i);
    if (std::find(this->dataPtr->ignoreModels.begin(),
                  this->dataPtr->ignoreModels.end(), model->GetName()) !=
                  this->dataPtr->ignoreModels.end())
    {
      continue;
    }

    // Obstacle's bounding box
    auto bb = model->BoundingBox();

    // Increase box by margin
    bb.Min() -= ignition::math::Vector3d::One * this->dataPtr->obstacleMargin;
    bb.Max() += ignition::math::Vector3d::One * this->dataPtr->obstacleMargin;

    // Increase vertically
    bb.Min().Z() -= 5;
    bb.Max().Z() += 5;

    // Check
    if (bb.Contains(actorPose))
    {
      return true;
    }

    // TODO: Improve obstacle avoidance. Some ideas: check contacts, ray-query
    // the path forward, check against bounding box of each collision shape...
  }
  return false;
}

/////////////////////////////////////////////////
void FollowActorPlugin::OnUpdate(const common::UpdateInfo &_info)
{
  // Time delta
  double dt = (_info.simTime - this->dataPtr->lastUpdate).Double();

  this->dataPtr->lastUpdate = _info.simTime;

  // Is there a follow target?
  if (!this->dataPtr->target)
    return;

  // Don't move if there's an obstacle on the way
  if (this->ObstacleOnTheWay())
    return;

  // Current pose - actor is oriented Y-up and Z-front
  auto actorPose = this->dataPtr->actor->WorldPose();
  auto zPos = actorPose.Pos().Z();

  // Current target
  auto targetPose = this->dataPtr->target->WorldPose();

  // Direction to target
  auto dir = targetPose.Pos() - actorPose.Pos();

  // Stop if too close to target
  if (dir.Length() <= this->dataPtr->minDistance)
    return;

  // Stop following if too far from target
  if (dir.Length() > this->dataPtr->maxDistance)
  {
    gzwarn << "Robot too far, guest stopped following" << std::endl;
    this->dataPtr->target = nullptr;
    return;
  }

  // Difference to target
  ignition::math::Angle yaw = atan2(dir.Y(), dir.X()) + IGN_PI_2;
  yaw.Normalize();
  dir.Normalize();

  actorPose.Pos() += dir * this->dataPtr->velocity * dt;
  actorPose.Pos().Z(zPos);
  actorPose.Rot() = ignition::math::Quaterniond(IGN_PI_2, 0, yaw.Radian());

  // Distance traveled is used to coordinate motion with the walking
  // animation
  double distanceTraveled = (actorPose.Pos() -
      this->dataPtr->actor->WorldPose().Pos()).Length();

  // Update actor
  this->dataPtr->actor->SetWorldPose(actorPose, false, false);
  this->dataPtr->actor->SetScriptTime(this->dataPtr->actor->ScriptTime() +
    (distanceTraveled * this->dataPtr->animationFactor));
}

/////////////////////////////////////////////////
bool FollowActorPlugin::OnPickUpRosRequest(
    servicesim_competition::PickUpGuest::Request &_req,
    servicesim_competition::PickUpGuest::Response &_res)
{
  _res.success = true;

  // Requesting the correct guest?
  auto actorName = _req.guest_name;
  if (actorName != this->dataPtr->actor->GetName())
  {
    gzwarn << "Wrong guest name: [" << actorName << "]" << std::endl;
    _res.success = false;
    return false;
  }

  // Get target model (robot)
  auto targetName = _req.robot_name;

  auto world = this->dataPtr->actor->GetWorld();

  auto model = world->ModelByName(targetName);
  if (!model)
  {
    gzwarn << "Failed to find model: [" << targetName << "]" << std::endl;
    _res.success = false;
    return false;
  }

  // Check pickup radius
  auto actorPos = this->dataPtr->actor->WorldPose().Pos();
  auto targetPos = model->WorldPose().Pos();

  auto posDiff = actorPos - targetPos;
  posDiff.Z(0);

  if (posDiff.Length() > this->dataPtr->pickUpRadius)
  {
    gzwarn << "Robot too far from guest" << std::endl;
    _res.success = false;
    return false;
  }

  this->dataPtr->target = model;
  return true;
}

/////////////////////////////////////////////////
bool FollowActorPlugin::OnDropOffRosRequest(
    servicesim_competition::DropOffGuest::Request &_req,
    servicesim_competition::DropOffGuest::Response &_res)
{
  _res.success = true;

  // Requesting the correct guest?
  auto actorName = _req.guest_name;
  if (actorName != this->dataPtr->actor->GetName())
  {
    gzwarn << "Wrong guest name: [" << actorName << "]" << std::endl;
    _res.success = false;
    return false;
  }

  this->dataPtr->target = nullptr;
  return true;
}
