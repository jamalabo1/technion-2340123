cmake_minimum_required(VERSION 3.20)
project(skeleton_smash LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

FILE(GLOB headers "include/*.h")

add_library(skeleton_smash_lib
        OBJECT

        include/ds/avl.h
        impl/ds/avl.cpp
        impl/ds/avl_init.cpp

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
        impl/command_factory.cpp
        impl/scoped/fd.cpp
        impl/scoped/dup2.cpp
        impl/scoped/dup.cpp


)

target_include_directories(skeleton_smash_lib
        PUBLIC
        include
        include/commands
        include/ds
        include/services
        include/scoped
)

add_executable(skeleton_smash
        smash.cpp
)

target_link_libraries(skeleton_smash PUBLIC skeleton_smash_lib)

add_executable(test_err test_err.cpp)


include(tester.cmake)

ds_tester_attach(skeleton_smash_lib HW tests)

enable_testing()
