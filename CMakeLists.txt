cmake_minimum_required(VERSION 3.15)
project(gravity_sandbox CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

add_subdirectory(vendor/raylib)

add_executable(gravity_sandbox
    src/main.cpp
    src/physics.cpp
    src/patterns.cpp
    src/ui.cpp)
target_link_libraries(gravity_sandbox PRIVATE raylib)

if(APPLE)
    target_link_libraries(gravity_sandbox PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()

if(EMSCRIPTEN)
    set_target_properties(gravity_sandbox PROPERTIES SUFFIX ".html")
    target_link_options(gravity_sandbox PRIVATE
        "-sUSE_GLFW=3"
        "-sALLOW_MEMORY_GROWTH=1"
        "--preload-file" "${CMAKE_SOURCE_DIR}/assets@assets"
        "--shell-file" "${CMAKE_SOURCE_DIR}/web/shell.html")
    set_target_properties(gravity_sandbox PROPERTIES
        LINK_DEPENDS "${CMAKE_SOURCE_DIR}/web/shell.html")
endif()

add_custom_command(TARGET gravity_sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:gravity_sandbox>/assets)
