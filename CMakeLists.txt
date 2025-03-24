cmake_minimum_required(VERSION 3.15)
project(WebGPU_Engine CXX)

include(utils.cmake)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(DEV_MODE "Set up development helper settings" ON)

# Include Conan-generated files
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/generators/conan_toolchain.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/build/generators/conan_toolchain.cmake")
else()
	message(WARNING "Conan Toolchain was not found at:\n\t${CMAKE_CURRENT_SOURCE_DIR}/build/generators/conan_toolchain.cmake")
endif()

# Use Conan dependencies
find_package(glm REQUIRED CONFIG)
find_package(sdl2 REQUIRED CONFIG)

# External dependencies
if (NOT EMSCRIPTEN)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/sdl2webgpu")
endif()
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/webgpu")

# Define the executable
add_executable(WebGPU_Engine src/main.cpp)

# Handle Debug and Release modes correctly
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Configuring Debug build")

    if(MSVC)
        target_compile_options(WebGPU_Engine PRIVATE /Zi /Od)  # /Zi for debug symbols, /Od for no optimization
        target_link_options(WebGPU_Engine PRIVATE /DEBUG)       # Ensure debug information in .pdb file
    else()
        target_compile_options(WebGPU_Engine PRIVATE -g -O0)    # -g for debug symbols (GCC/Clang), -O0 to disable optimizations
    endif()

    target_compile_definitions(WebGPU_Engine PRIVATE RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources")
else()
    message(STATUS "Configuring Release build")

    if(MSVC)
        target_compile_options(WebGPU_Engine PRIVATE /O2)  # /O2 for optimization
    else()
        target_compile_options(WebGPU_Engine PRIVATE -O3)  # -O3 for optimization in GCC/Clang
    endif()

    target_compile_definitions(WebGPU_Engine PRIVATE RESOURCE_DIR="./resources")
endif()


# Emscripten setup (This will only work if you have Emscripten installed and set up)
if(EMSCRIPTEN)
    message(STATUS "Building with Emscripten")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s USE_WEBGPU=1 -s WASM=1")
endif()


# Link Libraries
target_link_libraries(WebGPU_Engine PRIVATE webgpu)
target_link_libraries(WebGPU_Engine PRIVATE glm::glm)
if (NOT EMSCRIPTEN)
    target_link_libraries(WebGPU_Engine PRIVATE SDL2::SDL2 sdl2webgpu)
endif()


set_target_properties(WebGPU_Engine PROPERTIES
	CXX_STANDARD 17
	VS_DEBUGGER_ENVIRONMENT "DAWN_DEBUG_BREAK_ON_ERROR=1"
)

if (MSVC)
	# Ignore a warning that GLM requires to bypass
	# Disable warning C4201: nonstandard extension used: nameless struct/union
	target_compile_options(WebGPU_Engine PUBLIC /wd4201)
	# Disable warning C4305: truncation from 'int' to 'bool' in 'if' condition
	target_compile_options(WebGPU_Engine PUBLIC /wd4305)

	# Ignore a warning that stb_image requires to bypass
	# Disable warning C4244: conversion from 'int' to 'short', possible loss of data
	target_compile_options(WebGPU_Engine PUBLIC /wd4244)
endif (MSVC)

if (EMSCRIPTEN)
	# Add Emscripten-specific link options
	target_link_options(WebGPU_Engine PRIVATE
		-sUSE_GLFW=3 # Use Emscripten-provided GLFW
		-sUSE_WEBGPU # Handle WebGPU symbols
		-sASYNCIFY # Required by WebGPU-C++
		-sALLOW_MEMORY_GROWTH
		--preload-file "${CMAKE_CURRENT_SOURCE_DIR}/resources"
	)

	# Generate a full web page rather than a simple WebAssembly module
	set_target_properties(WebGPU_Engine PROPERTIES SUFFIX ".html")
endif()

target_copy_webgpu_binaries(WebGPU_Engine)