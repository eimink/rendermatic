cmake_minimum_required(VERSION 3.28)
project(rendermatic)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Find or fetch GLFW
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.3.8
    )
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)
endif()

# Fetch and configure GLAD
include(FetchContent)
FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v0.1.36
)
FetchContent_GetProperties(glad)
if(NOT glad_POPULATED)
    FetchContent_Populate(glad)
    set(GLAD_PROFILE "core" CACHE STRING "OpenGL profile")
    set(GLAD_API "gl=3.3" CACHE STRING "API type/version pairs")
    set(GLAD_GENERATOR "c" CACHE STRING "Language to generate the binding for")
    add_subdirectory(${glad_SOURCE_DIR} ${glad_BINARY_DIR})
endif()

# Find DirectFB (for Linux systems)
if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DirectFB REQUIRED directfb)
    if(NOT DirectFB_FOUND)
        message(STATUS "DirectFB not found. To install, clone https://github.com/directfb/DirectFB.git and build with:\n"
                      "meson setup build\n"
                      "meson compile -C build\n"
                      "sudo meson install -C build")
    else()
        message(STATUS "DirectFB include dirs: ${DirectFB_INCLUDE_DIRS}")
        # Verify directfb.h exists in the include path
        find_file(DIRECTFB_HEADER "directfb.h" PATHS ${DirectFB_INCLUDE_DIRS})
        if(NOT DIRECTFB_HEADER)
            message(FATAL_ERROR "DirectFB headers not found in ${DirectFB_INCLUDE_DIRS}")
        else()
            message(STATUS "Found directfb.h at: ${DIRECTFB_HEADER}")
        endif()
    endif()
endif()

# Add executable with conditional sources
set(SOURCES
    main.cpp
    loader.cpp
    ndireceiver.cpp
)

if(DirectFB_FOUND)
    list(APPEND SOURCES dfb_renderer.cpp)
    add_compile_definitions(HAVE_DIRECTFB)
    # Ensure GLAD is built with DirectFB support
    set(GLAD_GL_IMPLEMENTATION "gl")
    set(GLAD_EGL_IMPLEMENTATION "egl")
endif()

list(APPEND SOURCES glfw_renderer.cpp)

add_executable(${PROJECT_NAME} ${SOURCES})

# Link libraries with conditional DirectFB
target_link_libraries(${PROJECT_NAME} 
    PRIVATE
    glfw
    glad
)

if(DirectFB_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${DirectFB_LIBRARIES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${DirectFB_INCLUDE_DIRS})
endif()

# Include directories
target_include_directories(${PROJECT_NAME} 
    PRIVATE
    ${glad_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Platform specific configurations
if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE GL dl)
elseif(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE 
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreFoundation"
    )
endif()

# Set compiler warnings
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()

# Copy shaders and textures to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/shaders DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/textures DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
