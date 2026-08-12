cmake_minimum_required(VERSION 3.15)
project(led_group C)

add_library(led_group STATIC src/led_group.c)
target_include_directories(led_group PUBLIC include)
target_compile_features(led_group PUBLIC c_std_99)

option(LED_GROUP_BUILD_TESTS "Build led_group unit tests" ON)
option(LED_GROUP_BUILD_EXAMPLES "Build led_group examples" ON)

if(LED_GROUP_BUILD_TESTS AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_subdirectory(tests)
endif()

if(LED_GROUP_BUILD_EXAMPLES AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory(examples)
endif()
