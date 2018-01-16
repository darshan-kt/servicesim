# Dependencies

* Gazebo 8
* ROS Lunar

# Build

## Plugins

    cd servicesim/plugins
    mkdir build
    cd build
    cmake ..
    make

## Worlds

    cd servicesim/worlds
    erb service.world.erb > service.world

# Run service world

    cd servicesim
    GAZEBO_MODEL_PATH=`pwd`/models:$GAZEBO_MODEL_PATH GAZEBO_PLUGIN_PATH=`pwd`/plugins/build gazebo --verbose worlds/service.world

# Run actor creation world

    cd servicesim
    GAZEBO_MODEL_PATH=`pwd`/models:$GAZEBO_MODEL_PATH GAZEBO_PLUGIN_PATH=`pwd`/plugins/build gazebo --verbose worlds/create_actor.world
