cmake_minimum_required(VERSION 3.25)
project(Adaptive_And_Load_Balanced_Video_Streaming_CDN)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Release
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")

# Ensure that output executables are in the build/bin directory
SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)

# Use local dependencies instead of fetching
add_subdirectory(${CMAKE_SOURCE_DIR}/../deps/pugixml ${CMAKE_BINARY_DIR}/pugixml)
add_subdirectory(${CMAKE_SOURCE_DIR}/../deps/spdlog ${CMAKE_BINARY_DIR}/spdlog)
add_subdirectory(${CMAKE_SOURCE_DIR}/../deps/cxxopts ${CMAKE_BINARY_DIR}/cxxopts)

find_package(Boost REQUIRED COMPONENTS regex)

# Recurse through the subdirectories
add_subdirectory(src)
