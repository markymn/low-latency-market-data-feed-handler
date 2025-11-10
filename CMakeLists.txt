cmake_minimum_required(VERSION 3.15)
project(FeedHandler CXX)

# Use C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -Wall -Wextra")

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# Main executable
add_executable(feed_handler
    src/main.cpp
)