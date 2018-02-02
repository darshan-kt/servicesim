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

#include <gazebo/common/Console.hh>
#include <gazebo/physics/PhysicsIface.hh>
#include <gazebo/physics/World.hh>

#include "Checkpoint.hh"

using namespace servicesim;

/////////////////////////////////////////////////
Checkpoint::Checkpoint(const sdf::ElementPtr &_sdf, const unsigned int _number)
{
  this->number = _number;
  this->weight = _sdf->Get<double>("weight");
}

/////////////////////////////////////////////////
double Checkpoint::Score() const
{
  auto end = this->endTime;

  // If not finished yet
  if (end == gazebo::common::Time::Zero)
    end = gazebo::physics::get_world()->SimTime();

  auto elapsedSeconds = (end - this->startTime).Double();

  return elapsedSeconds * this->weight;
}

/////////////////////////////////////////////////
void Checkpoint::Start()
{
  this->startTime = gazebo::physics::get_world()->SimTime();

  gzmsg << "[ServiceSim] Started Checkpoint " << this->number << " at "
    << this->startTime.FormattedString(gazebo::common::Time::HOURS,
                                       gazebo::common::Time::MILLISECONDS)
    << std::endl;
}

/////////////////////////////////////////////////
ContainCheckpoint::ContainCheckpoint(const sdf::ElementPtr &_sdf,
    const unsigned int _number) : Checkpoint(_sdf, _number)
{
  if (!_sdf->HasElement("namespace"))
    gzwarn << "Missing <namespace> for contain checkpoint" << std::endl;
  else
    this->ns = _sdf->Get<std::string>("namespace");
}

/////////////////////////////////////////////////
bool ContainCheckpoint::Check()
{
  // Call enable service
  std::function<void(const ignition::msgs::Boolean &, const bool)> cb =
      [this](const ignition::msgs::Boolean &, const bool _result)
  {
    if (_result)
      this->enabled = !this->enabled;
  };

  // First time checking
  if (!this->enabled && !this->containDone)
  {
    // Setup contain subscriber
    this->ignNode.Subscribe(this->ns + "/contain",
        &ContainCheckpoint::OnContain, this);

    // Enable contain plugin
    ignition::msgs::Boolean req;
    req.set_data(true);
    this->ignNode.Request(this->ns + "/enable", req, cb);
  }

  if (this->enabled && this->containDone)
  {
    // Unsubscribe
    for (auto const &sub : this->ignNode.SubscribedTopics())
      this->ignNode.Unsubscribe(sub);

    // Disable contain plugin
    ignition::msgs::Boolean req;
    req.set_data(false);
    this->ignNode.Request(this->ns + "/enable", req, cb);
  }

  return this->containDone;
}

//////////////////////////////////////////////////
void ContainCheckpoint::OnContain(const ignition::msgs::Boolean &_msg)
{
  this->containDone = _msg.data();
}
