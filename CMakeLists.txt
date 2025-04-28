cmake_minimum_required(VERSION 3.15)
project(
    WebGPU_Engine
    VERSION 0.1.0
    LANGUAGES CXX C
)

include(utils.cmake)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Determine if this is a Web (Emscripten) build
if(EMSCRIPTEN)
    set(CONAN_PLATFORM "Emscripten")
else()
    set(CONAN_PLATFORM "${CMAKE_SYSTEM_NAME}")
endif()


set(CONAN_GENERATORS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/${CONAN_PLATFORM}/${CMAKE_BUILD_TYPE}/generators")

# Include the Conan toolchain if it exists
set(CONAN_TOOLCHAIN_FILE "${CONAN_GENERATORS_DIR}/conan_toolchain.cmake")
if(EXISTS "${CONAN_TOOLCHAIN_FILE}")
    message(STATUS "Using Conan toolchain from:\n\t${CONAN_TOOLCHAIN_FILE}")
    include("${CONAN_TOOLCHAIN_FILE}")
else()
    message(WARNING "Conan Toolchain was not found at:\n\t${CONAN_TOOLCHAIN_FILE}")
endif()

# Setup SDL2 for Emscripten
if(EMSCRIPTEN)
    add_library(SDL2_SDL2 INTERFACE)
    target_compile_options(SDL2_SDL2 INTERFACE -sUSE_SDL=2)
    target_link_options(SDL2_SDL2 INTERFACE -sUSE_SDL=2)
    add_library(SDL2::SDL2 ALIAS SDL2_SDL2)
else()
    find_package(sdl2 REQUIRED CONFIG)
endif()

# Use Conan dependencies
find_package(glm REQUIRED CONFIG)

# External dependencies
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/sdl2webgpu")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/webgpu")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/imgui")

# Define the executable
add_executable(WebGPU_Engine
    src/main.cpp
    src/Application.h
    src/Application.cpp
    src/ResourceManager.h
    src/ResourceManager.cpp
    src/implementations.cpp
)

# Handle Debug and Release modes correctly
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Configuring Debug build")

    if(MSVC)
        target_compile_options(WebGPU_Engine PRIVATE /Zi /Od) # /Zi for debug symbols, /Od for no optimization
        target_link_options(WebGPU_Engine PRIVATE /DEBUG) # Ensure debug information in .pdb file
    else()
        target_compile_options(WebGPU_Engine PRIVATE -g -O0 -ferror-limit=0) # -g for debug symbols (GCC/Clang), -O0 to disable optimizations
    endif()

    target_compile_definitions(WebGPU_Engine PRIVATE RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources")
else()
    message(STATUS "Configuring Release build")

    if(MSVC)
        target_compile_options(WebGPU_Engine PRIVATE /O2) # /O2 for optimization
    else()
        target_compile_options(WebGPU_Engine PRIVATE -O3) # -O3 for optimization in GCC/Clang
    endif()

    target_compile_definitions(WebGPU_Engine PRIVATE RESOURCE_DIR="./resources")
endif()

# Link Libraries
target_link_libraries(WebGPU_Engine PRIVATE webgpu)
target_link_libraries(WebGPU_Engine PRIVATE glm::glm)
target_link_libraries(WebGPU_Engine PRIVATE SDL2::SDL2 sdl2webgpu)
target_link_libraries(WebGPU_Engine PRIVATE imgui)
if(NOT EMSCRIPTEN)
    target_compile_definitions(imgui PRIVATE IMGUI_IMPL_WEBGPU_BACKEND_WGPU)
endif()



set_target_properties(WebGPU_Engine PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    COMPILE_WARNING_AS_ERROR ON
    VS_DEBUGGER_ENVIRONMENT "DAWN_DEBUG_BREAK_ON_ERROR=1"
)

if(XCODE)
    set_target_properties(WebGPU_Engine PROPERTIES
        XCODE_GENERATE_SCHEME ON
        XCODE_SCHEME_ENABLE_GPU_FRAME_CAPTURE_MODE "Metal"
    )
endif()

if(MSVC)
    # Ignore a warning that GLM requires to bypass
    # Disable warning C4201: nonstandard extension used: nameless struct/union
    target_compile_options(WebGPU_Engine PUBLIC /wd4201)
    # Disable warning C4305: truncation from 'int' to 'bool' in 'if' condition
    target_compile_options(WebGPU_Engine PUBLIC /wd4305)

    # Ignore a warning that stb_image requires to bypass
    # Disable warning C4244: conversion from 'int' to 'short', possible loss of data
    target_compile_options(WebGPU_Engine PUBLIC /wd4244)
endif(MSVC)

if(EMSCRIPTEN)
    set(CMAKE_SYSROOT "$ENV{EMSDK}/upstream/emscripten/cache/sysroot")
    message(STATUS "Using Emscripten sysroot: ${CMAKE_SYSROOT}")

    target_include_directories(WebGPU_Engine PRIVATE "${CMAKE_SYSROOT}/include")

    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s WASM=1")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s USE_WEBGPU=1")

    # Generate a full web page rather than a simple WebAssembly module
    set_target_properties(WebGPU_Engine PROPERTIES SUFFIX ".html")

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(STATUS "Configuring Debug-specific Emscripten options...")

        target_link_options(WebGPU_Engine PRIVATE
            -sUSE_WEBGPU
            -sASYNCIFY
            -sALLOW_MEMORY_GROWTH
            --preload-file "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            -gsource-map
            -sDEMANGLE_SUPPORT=1
            --source-map-base "http://localhost:8080/build/Emscripten/Debug/"
        )
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DWGPU_LOG=trace")

        # Copy sources (src, external) for better debugging
        add_custom_command(TARGET WebGPU_Engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/src"
                "${CMAKE_CURRENT_BINARY_DIR}/src"
        )
        add_custom_command(TARGET WebGPU_Engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/external"
                "${CMAKE_CURRENT_BINARY_DIR}/external"
        )
    else()
        message(STATUS "Configuring Release-specific Emscripten options...")

        target_link_options(WebGPU_Engine PRIVATE
            -sUSE_WEBGPU
            -sASYNCIFY
            -sALLOW_MEMORY_GROWTH
            --preload-file "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        )
    endif()
endif()


# Copy SDL2 and WebGPU binaries to the build directory
target_copy_webgpu_binaries(WebGPU_Engine)
