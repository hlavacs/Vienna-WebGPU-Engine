cmake_minimum_required(VERSION 3.15)
project(RuntimePlayer VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find the Vienna WebGPU Engine library
if(NOT TARGET WebGPU_Engine_Lib)
    get_filename_component(ENGINE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
    add_subdirectory(${ENGINE_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/engine)
endif()

# A project's custom C++ scripts (behaviour node types) are compiled straight
# into the game. Build Game sets the PROJECT_SCRIPTS_DIR env var to the project's
# scripts/ folder; each .cpp there self-registers its node types via a static
# initializer, so no explicit hook is needed. Unset = plain player (no-op).
set(RUNTIME_PLAYER_SOURCES main.cpp)
if(DEFINED ENV{PROJECT_SCRIPTS_DIR} AND EXISTS "$ENV{PROJECT_SCRIPTS_DIR}")
    file(GLOB PROJECT_SCRIPT_SOURCES CONFIGURE_DEPENDS "$ENV{PROJECT_SCRIPTS_DIR}/*.cpp")
    list(APPEND RUNTIME_PLAYER_SOURCES ${PROJECT_SCRIPT_SOURCES})
    list(LENGTH PROJECT_SCRIPT_SOURCES PROJECT_SCRIPT_COUNT)
    message(STATUS "RuntimePlayer: compiling ${PROJECT_SCRIPT_COUNT} project script(s) from $ENV{PROJECT_SCRIPTS_DIR}")
endif()

# Standalone player that loads a bundled project (assets/project.json) and runs.
add_engine_executable(RuntimePlayer
    SOURCES
    ${RUNTIME_PLAYER_SOURCES}
)
