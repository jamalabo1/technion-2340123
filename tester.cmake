# Guarded include
if (TARGET ds_tester)
    return()
endif ()

add_library(ds_tester INTERFACE)
add_library(ds_tester::ds_tester ALIAS ds_tester)

function(ds_tester_attach student_target)
    set(options)
    set(oneValueArgs HW)
    set(multiValueArgs)

    cmake_parse_arguments(DS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT TARGET ${student_target})
        message(FATAL_ERROR "Target ${student_target} not found, please refer to the docs")
    endif ()

    # Enforce common flags for fairness
    if (MSVC)
        message(FATAL_ERROR "The course does not support MSVC compiler")
    else ()
        #g++ -std=c++11 -DNDEBUG -Wall -o main.out *.cpp
        target_compile_options(${student_target} PRIVATE -Wall -DNDEBUG)
    endif ()

    message(STATUS "dir=${CMAKE_CURRENT_FUNCTION_LIST_DIR}")

    # get tests from the requested dir (released version will stip the other tests)
    set(TESTS_DIR ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/${DS_HW})

    message(STATUS "looking for tests in ${TESTS_DIR}")

    if (EXISTS ${TESTS_DIR})
        set(test_target ${DS_HW}_tests)

        file(GLOB_RECURSE test_SRCs CONFIGURE_DEPENDS "${TESTS_DIR}/*.cpp")

        # gtest prelude
        enable_testing()

        include(CTest)
        Include(FetchContent)


        FetchContent_Declare(
                googletest
                GIT_REPOSITORY https://github.com/google/googletest.git
                GIT_TAG        v1.14.0
                #                URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
                DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        # For Windows: Prevent overriding the parent project's compiler/linker settings
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(_ds_tester_previous_cxx_standard ${CMAKE_CXX_STANDARD})
        set(CMAKE_CXX_STANDARD 14)
        set(CMAKE_CXX_STANDARD_REQUIRED ON)
        FetchContent_MakeAvailable(googletest)
        set(CMAKE_CXX_STANDARD ${_ds_tester_previous_cxx_standard})

        # create the test executable
        add_executable(
                ${test_target}

                ${test_SRCs}
        )

        set_target_properties(${test_target} PROPERTIES
                CXX_STANDARD 14
                CXX_STANDARD_REQUIRED ON
        )

        # link to the test target
        target_link_libraries(
                ${test_target}
                PUBLIC
                ${student_target}
                GTest::gtest_main
        )

        #include the headers
        target_include_directories(${test_target} PRIVATE ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/include)

        if (TARGET skeleton_smash)
            add_dependencies(${test_target} skeleton_smash)
        endif ()

        include(GoogleTest)
        gtest_discover_tests(${test_target})
    else ()
        message(WARNING "${DS_HW} tests directory was not found!")
    endif ()
endfunction()
