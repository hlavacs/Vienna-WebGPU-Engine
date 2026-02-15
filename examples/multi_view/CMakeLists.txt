cmake_minimum_required(VERSION 3.15)
project(MultiView VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find the Vienna WebGPU Engine library
if(NOT TARGET WebGPU_Engine_Lib)
    # Assuming the engine is in the parent of parent directory
    get_filename_component(ENGINE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
    add_subdirectory(${ENGINE_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/engine)
endif()

add_engine_executable(MultiView
    SOURCES
    main.cpp
    FreeFlyCamera.cpp
    FreeFlyCamera.h
    CameraSwitcher.cpp
    CameraSwitcher.h
)
