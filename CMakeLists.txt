project(HW3-Server)
#cmake_minimum_required(VERSION 3.24)
#
#add_executable(server
#
#        server.c
#        request.c
#        log.c
#        segel.c
#)
#
#add_executable(client
#
#        client.c
#        segel.c
#)
#
#add_executable(
#        output
#
#        output.c
#)
#
#include_directories(.)

cmake_minimum_required(VERSION 3.16)

project(tiny_server C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-g -Wall)

find_package(Threads REQUIRED)

add_executable(server
        server.c
        request.c
        segel.c
        log.c
        requests_queue.c
        requests_queue.h
)

target_link_libraries(server PRIVATE Threads::Threads)

add_executable(client
        client.c
        segel.c
)

add_executable(output_cgi
        output.c
)

set_target_properties(output_cgi PROPERTIES
        OUTPUT_NAME "output.cgi"
)

add_custom_target(copy_public ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_CURRENT_BINARY_DIR}/public

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:output_cgi>
        ${CMAKE_CURRENT_BINARY_DIR}/public/output.cgi

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/favicon.ico
        ${CMAKE_CURRENT_BINARY_DIR}/public/favicon.ico

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/home.html
        ${CMAKE_CURRENT_BINARY_DIR}/public/home.html

        DEPENDS output_cgi
)