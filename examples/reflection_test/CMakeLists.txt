cmake_minimum_required(VERSION 3.15)
project(ReflectionTest VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT TARGET WebGPU_Engine_Lib)
    get_filename_component(ENGINE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
    add_subdirectory(${ENGINE_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/engine)
endif()

add_engine_executable(ReflectionTest
    SOURCES
    main.cpp
)
