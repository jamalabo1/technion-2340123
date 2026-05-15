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
        impl/commands/built_in_commands_impl.cpp
        impl/commands/external_commands.cpp

        impl/wenv.cpp
        include/parameters.h
        impl/ctx.cpp
        impl/command.cpp
        include/exceptions.h
        include/command_factory.h
        include/shell.h
        impl/shell.cpp
        impl/utils.cpp
        include/utils.h
        include/defines.h
        include/macros.h
        include/commands/external.h
        impl/job_table.cpp
        include/services.h
        include/command_proc.h
)


target_include_directories(skeleton_smash PUBLIC include)