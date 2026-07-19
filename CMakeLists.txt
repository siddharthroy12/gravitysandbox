cmake_minimum_required(VERSION 3.15)
project(gravity_sandbox CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

if(EMSCRIPTEN)
    # Keep the raylib submodule on its published upstream revision. This small
    # local patch gives the web canvas a device-pixel-ratio backing buffer and
    # maps pointer input back into CSS pixels.
    set(RAYLIB_WEB_HIDPI_PATCH "${CMAKE_SOURCE_DIR}/cmake/raylib-web-hidpi.patch")
    execute_process(
        COMMAND git -C "${CMAKE_SOURCE_DIR}/vendor/raylib" apply --check "${RAYLIB_WEB_HIDPI_PATCH}"
        RESULT_VARIABLE RAYLIB_WEB_PATCH_NEEDED
        OUTPUT_QUIET ERROR_QUIET)
    if(RAYLIB_WEB_PATCH_NEEDED EQUAL 0)
        execute_process(
            COMMAND git -C "${CMAKE_SOURCE_DIR}/vendor/raylib" apply "${RAYLIB_WEB_HIDPI_PATCH}"
            RESULT_VARIABLE RAYLIB_WEB_PATCH_RESULT)
        if(NOT RAYLIB_WEB_PATCH_RESULT EQUAL 0)
            message(FATAL_ERROR "Could not apply raylib web HiDPI patch")
        endif()
    else()
        execute_process(
            COMMAND git -C "${CMAKE_SOURCE_DIR}/vendor/raylib" apply --reverse --check "${RAYLIB_WEB_HIDPI_PATCH}"
            RESULT_VARIABLE RAYLIB_WEB_PATCH_PRESENT
            OUTPUT_QUIET ERROR_QUIET)
        if(NOT RAYLIB_WEB_PATCH_PRESENT EQUAL 0)
            message(FATAL_ERROR "Raylib submodule revision does not match the web HiDPI patch")
        endif()
    endif()
endif()

add_subdirectory(vendor/raylib)

add_executable(gravity_sandbox
    src/main.cpp
    src/curvature.cpp
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
