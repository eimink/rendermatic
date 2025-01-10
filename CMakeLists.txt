cmake_minimum_required(VERSION 3.28)
project(rendermatic)

# Enforce C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # Use -std=c++20 instead of -std=gnu++20

# Check if compiler supports C++20
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-std=c++20" COMPILER_SUPPORTS_CXX20)
if(NOT COMPILER_SUPPORTS_CXX20)
    message(FATAL_ERROR "The compiler ${CMAKE_CXX_COMPILER} does not support C++20")
endif()

# Set output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Only look for Wayland on Linux
if(UNIX AND NOT APPLE)
    # Make Wayland optional
    option(USE_WAYLAND "Enable Wayland support" ON)
    
    if(USE_WAYLAND)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(WAYLAND wayland-client wayland-egl)
        pkg_check_modules(EGL egl)
        pkg_check_modules(GLES glesv2)
        
        if(WAYLAND_FOUND AND EGL_FOUND AND GLES_FOUND)
            set(HAVE_WAYLAND TRUE)
            # Configure GLFW to use Wayland
            set(GLFW_USE_WAYLAND ON CACHE BOOL "Use Wayland for window creation" FORCE)
            set(GLFW_BUILD_X11 OFF CACHE BOOL "Build X11 support" FORCE)
            add_compile_definitions(USE_WAYLAND)
        else()
            message(STATUS "Wayland or its dependencies not found, falling back to X11")
            set(USE_WAYLAND OFF)
        endif()
    endif()
endif()

# Find or fetch GLFW with platform-appropriate settings
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

# Add ASIO first (before WebSocket++)
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-28-0
)
FetchContent_MakeAvailable(asio)

# Add WebSocket++ and its dependencies
find_package(Boost REQUIRED COMPONENTS system)
FetchContent_Declare(
    websocketpp
    GIT_REPOSITORY https://github.com/zaphoyd/websocketpp.git
    GIT_TAG 0.8.2
)
FetchContent_MakeAvailable(websocketpp)

# Add JsonCpp using FetchContent
FetchContent_Declare(
    jsoncpp
    GIT_REPOSITORY https://github.com/open-source-parsers/jsoncpp.git
    GIT_TAG 1.9.5
)
set(JSONCPP_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_CMAKE_PACKAGE ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(jsoncpp)

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
        find_file(DIRECTFB_HEADER "directfb.h" PATHS ${DirectFB_INCLUDE_DIRS})
        if(NOT DIRECTFB_HEADER)
            message(FATAL_ERROR "DirectFB headers not found in ${DirectFB_INCLUDE_DIRS}")
        else()
            message(STATUS "Found directfb.h at: ${DIRECTFB_HEADER}")
            add_compile_definitions(HAVE_DIRECTFB)
        endif()
    endif()
endif()

# Add executable with conditional sources
set(SOURCES
    main.cpp
    loader.cpp
    ndireceiver.cpp
    glfw_renderer.cpp
    config.cpp
    texture_manager.cpp
    websocket_server.cpp
)

if(DirectFB_FOUND)
    list(APPEND SOURCES 
        dfb_renderer.cpp
        dfb_pure_renderer.cpp
    )
    # Ensure GLAD is built with DirectFB support
    set(GLAD_GL_IMPLEMENTATION "gl")
    set(GLAD_EGL_IMPLEMENTATION "egl")
endif()

add_executable(${PROJECT_NAME} ${SOURCES})

# Link libraries with conditional DirectFB
target_link_libraries(${PROJECT_NAME} 
    PRIVATE
    glfw
    glad
    jsoncpp_static
    Boost::system
)

if(HAVE_WAYLAND)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${WAYLAND_LIBRARIES}
        ${EGL_LIBRARIES}
        ${GLES_LIBRARIES}
    )
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${WAYLAND_INCLUDE_DIRS}
        ${EGL_INCLUDE_DIRS}
        ${GLES_INCLUDE_DIRS}
    )
endif()

if(DirectFB_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${DirectFB_LIBRARIES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${DirectFB_INCLUDE_DIRS})
endif()

# Include directories
target_include_directories(${PROJECT_NAME} 
    PRIVATE
    ${glad_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${websocketpp_SOURCE_DIR}
    ${asio_SOURCE_DIR}/asio/include
    ${jsoncpp_SOURCE_DIR}/include
    ${jsoncpp_BINARY_DIR}/include
)

# Platform specific configurations
if(UNIX AND NOT APPLE)
    if(NOT HAVE_WAYLAND)
        target_link_libraries(${PROJECT_NAME} PRIVATE GL X11 dl)
    else()
        target_link_libraries(${PROJECT_NAME} PRIVATE dl)
    endif()
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
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /Zc:__cplusplus /std:c++20)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic -std=c++20)
endif()

# Copy shaders and textures to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/shaders DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/textures DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/config.json DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

# Add compile definitions for Wayland
target_compile_definitions(${PROJECT_NAME} 
    PRIVATE 
    ASIO_STANDALONE
    _WEBSOCKETPP_CPP11_INTERNAL_
    $<$<BOOL:${HAVE_WAYLAND}>:USE_WAYLAND>
)
