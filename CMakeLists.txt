cmake_minimum_required(VERSION 3.10)
project(skeleton_smash LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

#set(headers
#
#        include/wenv.h
#)
FILE(GLOB headers "include/*.h")

add_executable(skeleton_smash
        smash.cpp
        Commands.cpp
        signals.cpp

        ${headers}
        impl/built_in_commands_impl.cpp
        impl/wenv.cpp
        include/parameters.h
        impl/ctx.cpp
        impl/commands.cpp
        include/exceptions.h
)



target_include_directories(skeleton_smash PUBLIC include)