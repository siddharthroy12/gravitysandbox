cmake_minimum_required(VERSION 3.15)
project(gravity_sandbox CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

add_subdirectory(vendor/raylib)

add_executable(gravity_sandbox src/main.cpp)
target_link_libraries(gravity_sandbox PRIVATE raylib)

if(APPLE)
    target_link_libraries(gravity_sandbox PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()
