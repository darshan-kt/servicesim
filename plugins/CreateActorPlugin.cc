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

#ifdef _WIN32
  // Ensure that Winsock2.h is included before Windows.h, which can get
  // pulled in by anybody (e.g., Boost).
  #include <Winsock2.h>
#endif

#include <gazebo/common/ModelDatabase.hh>
#include <gazebo/rendering/Scene.hh>
#include <gazebo/transport/Node.hh>
#include <gazebo/transport/TransportIface.hh>
#include "CreateActorPlugin.hh"

std::map<std::string, std::string> skinMap;
std::map<std::string, std::string> animIdleMap;
std::map<std::string, std::string> animTrajectoryMap;
std::map<std::string, ignition::math::Pose3d> animPoseMap;

// Keep track of how many actors have been spawned
unsigned int actorCount{0};

// Ghost poses as string
std::vector<std::string> ghostPoses;

struct Current
{
  // Current skin DAE
  std::string skinDae{""};

  // Current animation DAE
  std::string animDae{""};

  // Pose offset for the current animDae
  ignition::math::Pose3d poseOffset{ignition::math::Pose3d::Zero};

  // Latest SDF as string
  std::string sdf{""};

  // Latest ERB as string
  std::string erb{""};
};

Current current;

namespace gazebo
{
  /// \brief Private data for the CreateActorPlugin class
  class CreateActorPluginPrivate
  {
    /// \brief Pointer to a node for communication.
    public: transport::NodePtr gzNode;

    /// \brief keyboard publisher.
    public: transport::PublisherPtr factoryPub;

    /// \brief Latest SDF
    public: std::string currentERB{""};
  };
}

using namespace gazebo;

// Register this plugin with the simulator
GZ_REGISTER_GUI_PLUGIN(CreateActorPlugin)

/////////////////////////////////////////////////
/// \brief Initiate the insertion of a ghost model
void insertGhost()
{
  auto filename = common::ModelDatabase::Instance()->GetModelFile(
      "model://ghost");
  gui::Events::createEntity("model", filename);
}

/////////////////////////////////////////////////
/// \brief Fill ghostPoses and delete ghosts
void processGhostPoses()
{
  ghostPoses.clear();

  std::string ghostPrefix{"ghost"};
  std::string ghostName{ghostPrefix};

  int count{0};

  while (rendering::get_scene()->GetVisual(ghostName))
  {
    // Get visual
    auto vis = rendering::get_scene()->GetVisual(ghostName);

    // Get pose
    auto pose = vis->WorldPose();

    // Apply offset
    pose = current.poseOffset + pose;

    // Convert to string
    std::ostringstream poseStr;
    poseStr << pose;
    ghostPoses.push_back(poseStr.str());

    // Delete ghost
    transport::requestNoReply("CreateActor", "entity_delete",
                              ghostName);

    // Next ghost
    ghostName = ghostPrefix + "_" + std::to_string(count++);
  }
}

/////////////////////////////////////////////////
/// \brief Fill SDF
void fillSDF()
{
  // Idle actors need one trajectory waypoint
  std::string trajectory;
  if (ghostPoses.size() == 1)
  {
    trajectory += "\
            <script>\n\
              <trajectory id='0' type='animation'>\n\
                <waypoint>\n\
                  <time>100</time>\n\
                  <pose>" + ghostPoses[0] + "</pose>\n\
                </waypoint>\n\
              </trajectory>\n\
            </script>\n";
  }
  // Trajectory actors use the plugin
  else
  {
    trajectory += "\
         <plugin name='wandering_plugin' filename='libWanderingActorPlugin.so'>\n\
           <target_weight>1.15</target_weight>\n\
           <obstacle_weight>1.8</obstacle_weight>\n\
           <animation_factor>5.1</animation_factor>\n";

    for (const auto &pose : ghostPoses)
    {
      trajectory += "\
             <target>" + pose + "</target>\n";
    }

    trajectory += "\
         </plugin>\n";
  }

  // Name
  auto name = "actor_" + std::to_string(actorCount++);

  current.sdf = "\
      <?xml version='1.0' ?>\n\
        <sdf version='" SDF_VERSION "'>\n\
          <actor name='" + name + "'>\n\
            <pose>" + ghostPoses[0] + "</pose>\n\
            <skin>\n\
              <filename>model://actor/meshes/" + current.skinDae + ".dae</filename>\n\
            </skin>\n\
            <animation name='animation'>\n\
              <filename>model://actor/meshes/" + current.animDae + ".dae</filename>\n";

  if (ghostPoses.size() > 1)
  {
    current.sdf += "\
              <interpolate_x>true</interpolate_x>\n";
  }

  current.sdf += "\
            </animation>\n\
            " + trajectory + "\n\
          </actor>\n\
        </sdf>";
}

/////////////////////////////////////////////////
/// \brief Fill ERB
void fillERB()
{
  current.erb = "TODO";
}

/////////////////////////////////////////////////
CreateActorPlugin::CreateActorPlugin()
  : GUIPlugin(), dataPtr(new CreateActorPluginPrivate)
{
  // Maps
  skinMap["Green shirt"] = "SKIN_man_green_shirt";
  skinMap["Red shirt"] = "SKIN_man_red_shirt";
  skinMap["Blue shirt"] = "SKIN_man_blue_shirt";

  animIdleMap["Talking A"] = "ANIMATION_talking_a";
  animPoseMap["Talking A"] = ignition::math::Pose3d(1, 0, -1.25, 0, 0, -IGN_PI_2);

  animIdleMap["Talking B"] = "ANIMATION_talking_b";
  animPoseMap["Talking B"] = ignition::math::Pose3d(1, 0, -1.25, 0, 0, IGN_PI_2);

  animTrajectoryMap["Walking"] = "ANIMATION_walking";
  animPoseMap["Walking"] = ignition::math::Pose3d(0, -1, -1.4, 0, 0, IGN_PI);

  animTrajectoryMap["Running"] = "ANIMATION_running";
  animPoseMap["Running"] = ignition::math::Pose3d(0, -1, -1.4, 0, 0, IGN_PI);

  // Stacked layout
  auto mainLayout = new QStackedLayout();

  // Frame
  auto frame = new QFrame();
  frame->setLayout(mainLayout);
  auto frameLayout = new QVBoxLayout();
  frameLayout->setContentsMargins(0, 0, 0, 0);
  frameLayout->addWidget(frame);
  this->setLayout(frameLayout);

  // Transport
  this->dataPtr->gzNode = transport::NodePtr(new transport::Node());
  this->dataPtr->gzNode->Init();
  this->dataPtr->factoryPub =
      this->dataPtr->gzNode->Advertise<msgs::Factory>("~/factory");

  // Skin combo
  auto skinCombo = new QComboBox();
  skinCombo->setObjectName("skinCombo");

  for (auto s : skinMap)
    skinCombo->addItem(QString::fromStdString(s.first));

  this->connect(skinCombo, static_cast<void (QComboBox::*)(const QString &)>(
      &QComboBox::currentIndexChanged), [=](const QString &_value)
  {
    current.skinDae = skinMap[_value.toStdString()];
  });
  current.skinDae = skinMap[skinCombo->currentText().toStdString()];

  // Animation combo
  auto animCombo = new QComboBox();
  animCombo->setObjectName("animCombo");

  for (auto a : animIdleMap)
    animCombo->addItem(QString::fromStdString(a.first));

  for (auto a : animTrajectoryMap)
    animCombo->addItem(QString::fromStdString(a.first));

  this->connect(animCombo, static_cast<void (QComboBox::*)(const QString &)>(
      &QComboBox::currentIndexChanged), [=](const QString &_value)
  {
    if (animIdleMap.find(_value.toStdString()) != animIdleMap.end())
    {
      current.animDae = animIdleMap[_value.toStdString()];
    }
    else
    {
      current.animDae = animTrajectoryMap[_value.toStdString()];
    }
    current.poseOffset = animPoseMap[_value.toStdString()];
  });
  current.animDae = animIdleMap[animCombo->currentText().toStdString()];
  current.poseOffset = animPoseMap[animCombo->currentText().toStdString()];

  // 0: Skin and animation
  {
    // Label
    auto label = new QLabel(tr("Choose skin and animation"));
    label->setMaximumHeight(50);

    // Next button
    auto nextButton = new QPushButton(tr("Next"));
    this->connect(nextButton, &QPushButton::clicked, [=]()
    {
      insertGhost();

      mainLayout->setCurrentIndex(1);
      actorCount++;
    });

    // Layout
    auto layout = new QGridLayout;
    layout->setSpacing(0);
    layout->addWidget(label, 0, 0, 1, 2);
    layout->addWidget(new QLabel("Skin"), 1, 0);
    layout->addWidget(skinCombo, 1, 1);
    layout->addWidget(new QLabel("Animation"), 2, 0);
    layout->addWidget(animCombo, 2, 1);
    layout->addWidget(nextButton, 3, 1);

    // Widget
    auto widget = new QWidget();
    widget->setLayout(layout);

    mainLayout->addWidget(widget);
  }

  // 1: Pose(s)
  {
    // Check animation type
    std::string animType{"idle"};

    // Label
    auto label = new QLabel(
        "Position the ghost and press Next when done.<br>\
         <b>Tip</b>: Use the translation and rotation tools.<br>\
         <b>You won't be able to reposition the actor after spawned</b>");
    label->setMaximumHeight(50);

    // Add button
    // TODO: hide for idle
    auto addButton = new QPushButton(tr("New waypoint"));
    this->connect(addButton, &QPushButton::clicked, [=]()
    {
      insertGhost();
    });

    // Next button
    auto nextButton = new QPushButton(tr("Next"));
    this->connect(nextButton, &QPushButton::clicked, [=]()
    {
      this->Spawn();
      mainLayout->setCurrentIndex(2);
    });

    // Layout
    auto layout = new QGridLayout;
    layout->setSpacing(0);
    layout->addWidget(label, 0, 0, 1, 2);
    layout->addWidget(addButton, 1, 1);
    layout->addWidget(nextButton, 3, 1);

    // Widget
    auto widget = new QWidget();
    widget->setLayout(layout);

    mainLayout->addWidget(widget);
  }

  // 2: Export
  {
    // Label
    auto label = new QLabel(
        "The actor has been spawned,<br>export to a file or start a new actor.");
    label->setMaximumHeight(50);

    // New actor
    auto newButton = new QPushButton(tr("New actor"));
    this->connect(newButton, &QPushButton::clicked, [=]()
    {
      mainLayout->setCurrentIndex(0);
    });

    // Export to SDF
    auto sdfButton = new QPushButton(tr("Export to SDF"));
    this->connect(sdfButton, &QPushButton::clicked, [=]()
    {
      // Choose destination file
      QFileDialog fileDialog(this, tr("Destination SDF file"), QDir::homePath());
      fileDialog.setFileMode(QFileDialog::AnyFile);
      fileDialog.setNameFilter("*.sdf");
      fileDialog.setAcceptMode(QFileDialog::AcceptSave);
      fileDialog.setOptions(QFileDialog::DontResolveSymlinks |
                            QFileDialog::DontUseNativeDialog);
      fileDialog.setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint |
          Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint);

      if (fileDialog.exec() != QDialog::Accepted)
        return;

      auto selected = fileDialog.selectedFiles();
      if (selected.empty())
        return;

      // Create dir
      {
        boost::filesystem::path path(selected[0].toStdString().substr(0,
                                     selected[0].toStdString().rfind("/")+1));

        if (!boost::filesystem::create_directories(path))
          gzerr << "Couldn't create folder [" << path << "]" << std::endl;
        else
          gzmsg << "Created folder [" << path << "]" << std::endl;
      }

      // Save model.sdf
      {
        std::ofstream savefile;
        savefile.open(selected[0].toStdString().c_str());
        if (!savefile.is_open())
        {
          gzerr << "Couldn't open file for writing: " << selected[0].toStdString() << std::endl;
          return;
        }
        savefile << current.sdf;
        savefile.close();
        gzdbg << "Saved file to " << selected[0].toStdString() << std::endl;
      }
    });

    // Export to ERB
    auto erbButton = new QPushButton(tr("Export to ERB"));
    this->connect(erbButton, &QPushButton::clicked, [=]()
    {
      // Choose destination file
      QFileDialog fileDialog(this, tr("Destination ERB file"), QDir::homePath());
      fileDialog.setFileMode(QFileDialog::AnyFile);
      fileDialog.setNameFilter("*.erb");
      fileDialog.setAcceptMode(QFileDialog::AcceptSave);
      fileDialog.setOptions(QFileDialog::DontResolveSymlinks |
                            QFileDialog::DontUseNativeDialog);
      fileDialog.setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint |
          Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint);

      if (fileDialog.exec() != QDialog::Accepted)
        return;

      auto selected = fileDialog.selectedFiles();
      if (selected.empty())
        return;

      // Create dir
      {
        boost::filesystem::path path(selected[0].toStdString().substr(0,
                                     selected[0].toStdString().rfind("/")+1));

        if (!boost::filesystem::create_directories(path))
          gzerr << "Couldn't create folder [" << path << "]" << std::endl;
        else
          gzmsg << "Created folder [" << path << "]" << std::endl;
      }

      // Save model.erb
      {
        std::ofstream savefile;
        savefile.open(selected[0].toStdString().c_str());
        if (!savefile.is_open())
        {
          gzerr << "Couldn't open file for writing: " << selected[0].toStdString() << std::endl;
          return;
        }
        savefile << this->dataPtr->currentERB;
        savefile.close();
        gzdbg << "Saved file to " << selected[0].toStdString() << std::endl;
      }
    });

    // Layout
    auto layout = new QGridLayout;
    layout->setSpacing(0);
    layout->addWidget(label, 0, 0, 1, 2);
    layout->addWidget(erbButton, 1, 0);
    layout->addWidget(sdfButton, 1, 1);
    layout->addWidget(newButton, 2, 0, 1, 2);

    // Widget
    auto widget = new QWidget();
    widget->setLayout(layout);

    mainLayout->addWidget(widget);
  }

  // Make this invisible
  this->move(1, 1);
  this->resize(450, 150);

  this->setStyleSheet(
      "QFrame {background-color: rgba(100, 100, 100, 255);\
               color: rgba(200, 200, 200, 255);}");
}

/////////////////////////////////////////////////
CreateActorPlugin::~CreateActorPlugin()
{
  this->dataPtr->factoryPub.reset();
  this->dataPtr->gzNode->Fini();
}

/////////////////////////////////////////////////
void CreateActorPlugin::Spawn()
{
  // Get ghost poses and delete them
  processGhostPoses();

  // Fill SDF
  fillSDF();

  // Fill ERB
  fillERB();

  // Spawn actor
  gazebo::msgs::Factory msg;
  msg.set_sdf(current.sdf);

  this->dataPtr->factoryPub->Publish(msg);
}

