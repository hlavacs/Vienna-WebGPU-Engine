cmake_minimum_required(VERSION 3.15)
set(ENGINE_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE INTERNAL "Engine root directory")

project(WebGPU_Engine VERSION 0.1.0 LANGUAGES C CXX)

include(utils.cmake)

# Options
option(BUILD_EXAMPLES "Build example projects" ON)

# C++ Standard and compiler settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# SDL2 setup (Web or Native)
if(EMSCRIPTEN)
    add_library(SDL2_SDL2 INTERFACE)
    target_compile_options(SDL2_SDL2 INTERFACE -sUSE_SDL=2)
    target_link_options(SDL2_SDL2 INTERFACE -sUSE_SDL=2)
    add_library(SDL2::SDL2 ALIAS SDL2_SDL2)
else()
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/SDL")
    
    if(APPLE)
        foreach(SDL_TARGET SDL2 SDL2-static SDL2main SDL2_test)
            if(TARGET ${SDL_TARGET})
                target_compile_options(${SDL_TARGET} PRIVATE -w)
            endif()
        endforeach()
    endif()
endif()

# External dependencies
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/sdl2webgpu")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/webgpu")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/imgui")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/spdlog")

# Add include dir so #include "engine/core/..." works everywhere
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Collect only engine source files (exclude main.cpp)
file(GLOB_RECURSE ENGINE_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/*.cpp"
)

# Build engine as a static library
add_library(WebGPU_Engine_Lib STATIC
    ${ENGINE_SOURCE_FILES}
)

# Make include directories public for library consumers
target_include_directories(WebGPU_Engine_Lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# glm is header-only — no build needed
target_include_directories(WebGPU_Engine_Lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/external/glm")

# tinygltf is header-only — no build needed
target_include_directories(WebGPU_Engine_Lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/external/tinygltf")

# Link libraries publicly so examples can use them
target_link_libraries(WebGPU_Engine_Lib PUBLIC
    SDL2::SDL2
    imgui
    sdl2webgpu
    webgpu
    spdlog
)

# Link SDL2main for Windows (required for proper WinMain entry point)
if(WIN32 AND NOT EMSCRIPTEN)
    if(TARGET SDL2::SDL2main)
        target_link_libraries(WebGPU_Engine_Lib PUBLIC SDL2::SDL2main)
    endif()
endif()

# imgui compiler warnings
if(MSVC)
    target_compile_options(imgui PRIVATE /w)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR EMSCRIPTEN)
    target_compile_options(imgui PRIVATE -Wno-nontrivial-memaccess)
else()
    target_compile_options(imgui PRIVATE -Wno-nontrivial-memcall)
endif()

# Build type options for library (use generator expressions for multi-config generators like Visual Studio)
if(MSVC)
    # Debug flags
    target_compile_options(WebGPU_Engine_Lib PUBLIC
        $<$<CONFIG:Debug>:/Zi /Od /RTC1 /diagnostics:caret>
        $<$<CONFIG:Release>:/O2>
    )
    target_compile_definitions(WebGPU_Engine_Lib PUBLIC
        $<$<CONFIG:Debug>:DEBUG_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}">
    )
else()
    # Non-MSVC compilers
    target_compile_options(WebGPU_Engine_Lib PUBLIC
        $<$<CONFIG:Debug>:-g -O0 -ferror-limit=0>
        $<$<CONFIG:Release>:-O3>
    )
    target_compile_definitions(WebGPU_Engine_Lib PUBLIC
        $<$<CONFIG:Debug>:DEBUG_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}">
    )
endif()

# Print build configuration message
if(CMAKE_BUILD_TYPE)
    message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
endif()

# General library properties
set_target_properties(WebGPU_Engine_Lib PROPERTIES
    COMPILE_WARNING_AS_ERROR ON
)

# MSVC-specific warnings to suppress
if(MSVC)
    target_compile_options(WebGPU_Engine_Lib PUBLIC /wd4201 /wd4305 /wd4244 /utf-8 /WX)
elseif(APPLE)
    # On Mac: suppress warnings but don't treat as errors
    target_compile_options(WebGPU_Engine_Lib PUBLIC
        -Wno-error                    # Never treat warnings as errors
        -Wno-deprecated-declarations  # Suppress deprecation warnings
        -Wno-unused-variable          # Suppress unused variable warnings
        -Wno-unused-parameter         # Suppress unused parameter warnings
        -Wno-gnu-folding-constant     # Suppress GNU extension warnings
    )
endif()

# imgui backend definition (if WGPU backend is used)
if(WEBGPU_BACKEND_WGPU)
    target_compile_definitions(imgui PRIVATE IMGUI_IMPL_WEBGPU_BACKEND_WGPU)
endif()

# Emscripten-specific configuration for library
if(EMSCRIPTEN)
    # Set compile options for library (propagates to consumers)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(WebGPU_Engine_Lib PUBLIC -g -gsource-map -gno-column-info)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DWGPU_LOG=trace")
    endif()

    # Base Emscripten link options that all consumers will need
    target_link_options(WebGPU_Engine_Lib INTERFACE
        -sUSE_WEBGPU
        -sASYNCIFY
        -sALLOW_MEMORY_GROWTH
        -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']
    )
endif()

# Helper function to create and configure an engine executable
function(add_engine_executable TARGET_NAME)
    # Parse arguments: source files and optional shell file
    set(options "")
    set(oneValueArgs SHELL_FILE)
    set(multiValueArgs SOURCES)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Create the executable
    add_executable(${TARGET_NAME} ${ARG_SOURCES})
    
    # Link against the engine
    target_link_libraries(${TARGET_NAME} PRIVATE WebGPU_Engine_Lib)
    
    # Set ASSETS_ROOT_DIR to the example's source directory
    target_compile_definitions(WebGPU_Engine_Lib PRIVATE
        $<$<CONFIG:Debug>:ASSETS_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}">
    )

    # Set default shell file if not provided
    if(NOT ARG_SHELL_FILE)
        set(ARG_SHELL_FILE "${ENGINE_ROOT_DIR}/src/shell_minimal.html")
    endif()

    if(EMSCRIPTEN)
        # Set HTML output
        set_target_properties(${TARGET_NAME} PROPERTIES SUFFIX ".html")

        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            target_link_options(${TARGET_NAME} PRIVATE
                --preload-file "${CMAKE_CURRENT_SOURCE_DIR}/resources"
                --shell-file "${ARG_SHELL_FILE}"
                -gsource-map
                --source-map-base "http://localhost:8080/build/Emscripten/Debug/"
            )

            # Copy source files for debugging
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/src"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/src")
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/external"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/external")
        else()
            target_link_options(${TARGET_NAME} PRIVATE
                --shell-file "${ARG_SHELL_FILE}"
            )
        endif()

        # Set shell file dependency
        set_property(TARGET ${TARGET_NAME}
            PROPERTY LINK_DEPENDS "${ARG_SHELL_FILE}"
        )
    else()
        # Native build: Copy DLLs and WebGPU binaries

        # Copy WebGPU binaries
        target_copy_webgpu_binaries(${TARGET_NAME})

        # Copy SDL2 DLL (if it exists as a shared library)
        if(TARGET SDL2::SDL2)
            # Get the actual SDL2 library file
            get_target_property(SDL2_DLL_LOCATION SDL2::SDL2 IMPORTED_LOCATION)
            if(NOT SDL2_DLL_LOCATION)
                # For multi-config generators, try to get the debug location
                get_target_property(SDL2_DLL_LOCATION SDL2::SDL2 IMPORTED_LOCATION_DEBUG)
            endif()
            if(NOT SDL2_DLL_LOCATION)
                # Try to find SDL2.dll in the build tree
                if(TARGET SDL2)
                    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_FILE:SDL2>
                        $<TARGET_FILE_DIR:${TARGET_NAME}>
                        COMMENT "Copying SDL2 DLL to output directory"
                    )
                endif()
            elseif(EXISTS "${SDL2_DLL_LOCATION}")
                add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${SDL2_DLL_LOCATION}"
                    $<TARGET_FILE_DIR:${TARGET_NAME}>
                    COMMENT "Copying SDL2 DLL to output directory"
                )
            endif()
        endif()

        # Set debugger environment
        set_target_properties(${TARGET_NAME} PROPERTIES
            VS_DEBUGGER_ENVIRONMENT "DAWN_DEBUG_BREAK_ON_ERROR=1"
        )

        # Xcode scheme for Metal capture
        if(XCODE)
            set_target_properties(${TARGET_NAME} PROPERTIES
                XCODE_GENERATE_SCHEME ON
                XCODE_SCHEME_ENABLE_GPU_FRAME_CAPTURE_MODE "Metal"
            )
        endif()
    endif()

    # Copy engine resources from root (Release builds only)
    if(EXISTS "${ENGINE_ROOT_DIR}/resources")
        if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${ENGINE_ROOT_DIR}/resources"
                $<TARGET_FILE_DIR:${TARGET_NAME}>/resources
                COMMENT "Copying engine resources")
        endif()
    endif()

    # Copy example assets from example directory (always)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            $<TARGET_FILE_DIR:${TARGET_NAME}>/assets
            COMMENT "Copying example assets")
    endif()
endfunction()