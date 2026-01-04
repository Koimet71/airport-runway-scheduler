cmake_minimum_required(VERSION 4.0)
project(multicoreproject_runwayscheduler)

set(CMAKE_CXX_STANDARD 14)

add_executable(multicoreproject_runwayscheduler
        scheduler.cpp
        flight.cpp
        main.cpp
        shared.h)
