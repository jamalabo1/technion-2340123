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

        include/ds/avl.h
        impl/ds/avl.cpp
        impl/ds/avl_init.cpp

        smash.cpp
        Commands.cpp
        signals.cpp

        ${headers}



        impl/commands/built_in_commands_impl.cpp
        impl/commands/external_commands.cpp


        impl/wenv.cpp
        impl/ctx.cpp
        impl/command.cpp
        impl/shell.cpp
        impl/utils.cpp
        impl/parameters.cpp


        impl/services/proc_mng.cpp
        impl/services/aliases.cpp
        impl/services/job_table.cpp
        impl/services/child_tracker.cpp


        impl/commands/builtin/foreground.cpp
        impl/commands/builtin/kill.cpp
        impl/commands/builtin/alias.cpp
        impl/commands/pipe_command.cpp
        impl/commands/redirect_command.cpp

        impl/commands/builtin/unset.cpp
        impl/commands/special/special_commands.cpp
        include/commands/special.h
        impl/commands/special/du_command.cpp


)

add_executable(test_err test_err.cpp)


target_include_directories(skeleton_smash PUBLIC include)