cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(pathfinder VERSION 1.0.0)

# Dev tools (debug overlay, verifier, gd-sim-test, pathfinder-cli) are left out of release builds;
# tools/build.ps1 turns them on.
option(PATHFINDER_DEV_TOOLS "Build the debug overlay, verifier and offline test tools" OFF)

add_library(${PROJECT_NAME} SHARED
    src/main.cpp
    src/pathfinder.cpp
    # Add any extra C++ source files here
)
if (PATHFINDER_DEV_TOOLS)
    target_sources(${PROJECT_NAME} PRIVATE src/debug.cpp src/verify.cpp)
endif()

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
add_subdirectory(gd-sim)

CPMAddPackage("gh:camila314/UIBuilder#main")
CPMAddPackage("gh:maxnut/GDReplayFormat#gdr2")

# add custom target to strip binary
if (APPLE)
add_custom_target(strip ALL
    COMMAND $<$<CONFIG:Debug>:echo> strip -x $<TARGET_FILE:${PROJECT_NAME}>
    DEPENDS ${PROJECT_NAME}
)
endif()


setup_geode_mod(${PROJECT_NAME})
target_compile_definitions(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:DEBUG_MODE=1>)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:-g> -O3 -fno-math-errno -fno-trapping-math -fvisibility=hidden -fomit-frame-pointer)
# clang-cl ignores -O3; optimise every config
if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /O2)
endif()
target_link_libraries(${PROJECT_NAME} gd-sim UIBuilder libGDR)

# Standalone pathfinder for tools/batch.py (no Geode). Build it in Release:
#   cmake --build build --config Release --target pathfinder-cli
if (PATHFINDER_DEV_TOOLS)
    add_executable(pathfinder-cli src/cli.cpp src/pathfinder.cpp)
    target_link_libraries(pathfinder-cli gd-sim libGDR)
endif()
