cmake_minimum_required(VERSION 3.20)
project(Tutorial LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find the engine package
find_package(ViennaWebGPUEngine REQUIRED)

# Tutorial executable
add_executable(Tutorial
	main.cpp
)

target_link_libraries(Tutorial PRIVATE ViennaWebGPUEngine::ViennaWebGPUEngine)

# Configure engine resources and example assets for this executable
configure_engine_executable(Tutorial 
	ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets"
)
