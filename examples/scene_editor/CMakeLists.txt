cmake_minimum_required(VERSION 3.15)
# C is enabled for tinyfiledialogs.c (vendored native file-dialog helper).
project(SceneEditor VERSION 1.0.0 LANGUAGES CXX C)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find the Vienna WebGPU Engine library
if(NOT TARGET WebGPU_Engine_Lib)
    # Assuming the engine is in the parent of parent directory
    get_filename_component(ENGINE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
    add_subdirectory(${ENGINE_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/engine)
endif()

add_engine_executable(SceneEditor
    SOURCES
    main.cpp
    SceneEditorUI.cpp
    SceneImporter.cpp
    EditorCameraController.cpp
    ImGuizmo.cpp
    tinyfiledialogs.c
)

# Native file dialogs: tinyfiledialogs shells out on macOS/Linux, but on Windows
# it calls GetOpenFileNameW (comdlg32) and the shell folder APIs (ole32/shell32).
if(WIN32)
    target_link_libraries(SceneEditor PRIVATE comdlg32 ole32 shell32)
endif()
