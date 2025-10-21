cmake_minimum_required(VERSION 3.15)
project(WebGPU_Engine VERSION 0.1.0 LANGUAGES C CXX)

include(utils.cmake)

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
endif()

# External dependencies
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/sdl2webgpu")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/webgpu")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/imgui")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/spdlog")

# Add include dir so #include "engine/core/..." works everywhere
include_directories(${CMAKE_SOURCE_DIR}/include)

file(GLOB_RECURSE SOURCE_FILES
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
)

# Main executable
add_executable(WebGPU_Engine
    ${SOURCE_FILES}
)

# glm is header-only — no build needed
target_include_directories(WebGPU_Engine PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/external/glm")

# tinygltf is header-only — no build needed
target_include_directories(WebGPU_Engine PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/external/tinygltf")

# Link libraries
target_link_libraries(WebGPU_Engine PRIVATE
    SDL2::SDL2
    imgui
    sdl2webgpu
    webgpu
	spdlog
)

# imgui compiler warnings
if(MSVC)
    target_compile_options(imgui PRIVATE /w)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR EMSCRIPTEN)
    target_compile_options(imgui PRIVATE -Wno-nontrivial-memaccess)
else()
    target_compile_options(imgui PRIVATE -Wno-nontrivial-memcall)
endif()

# Build type options
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Configuring Debug build")
    if(MSVC)
        target_compile_options(WebGPU_Engine PRIVATE /Zi /Od "/diagnostics:caret")
        target_link_options(WebGPU_Engine PRIVATE /DEBUG)
    else()
        target_compile_options(WebGPU_Engine PRIVATE -g -O0 -ferror-limit=0)
    endif()
    target_compile_definitions(WebGPU_Engine PRIVATE DEBUG_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(WebGPU_Engine PRIVATE ASSETS_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
else()
    message(STATUS "Configuring Release build")
    if(MSVC)
        target_compile_options(WebGPU_Engine PRIVATE /O2)
    else()
        target_compile_options(WebGPU_Engine PRIVATE -O3)
    endif()
	add_custom_command(TARGET WebGPU_Engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/resources"
                "${CMAKE_CURRENT_BINARY_DIR}/resources")
endif()


# General target properties
set_target_properties(WebGPU_Engine PROPERTIES
    COMPILE_WARNING_AS_ERROR ON
    VS_DEBUGGER_ENVIRONMENT "DAWN_DEBUG_BREAK_ON_ERROR=1"
)

# Xcode scheme for Metal capture
if(XCODE)
    set_target_properties(WebGPU_Engine PROPERTIES
        XCODE_GENERATE_SCHEME ON
        XCODE_SCHEME_ENABLE_GPU_FRAME_CAPTURE_MODE "Metal"
    )
endif()

# MSVC-specific warnings to suppress
if(MSVC)
    target_compile_options(WebGPU_Engine PRIVATE /wd4201 /wd4305 /wd4244)
endif()

# imgui backend definition (if WGPU backend is used)
if(WEBGPU_BACKEND_WGPU)
    target_compile_definitions(imgui PRIVATE IMGUI_IMPL_WEBGPU_BACKEND_WGPU)
endif()

# Emscripten-specific configuration
if(EMSCRIPTEN)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s WASM=1 -s USE_WEBGPU=1")
    set_target_properties(WebGPU_Engine PROPERTIES SUFFIX ".html")
	set(SHELL_FILE shell_minimal.html)

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(STATUS "Configuring Debug Emscripten options")
        target_link_options(WebGPU_Engine PRIVATE
            -sUSE_WEBGPU
            -sASYNCIFY
            -sALLOW_MEMORY_GROWTH
			-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']
            --preload-file "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    		--shell-file "${CMAKE_CURRENT_SOURCE_DIR}/src/${SHELL_FILE}"
            -gsource-map
            --source-map-base "http://localhost:8080/build/Emscripten/Debug/"
        )
		target_compile_options(WebGPU_Engine PRIVATE -g -gsource-map -gno-column-info)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DWGPU_LOG=trace")

        add_custom_command(TARGET WebGPU_Engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}/src")
        add_custom_command(TARGET WebGPU_Engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/external" "${CMAKE_CURRENT_BINARY_DIR}/external")
    else()
        message(STATUS "Configuring Release Emscripten options")
        target_link_options(WebGPU_Engine PRIVATE
            -sUSE_WEBGPU
            -sASYNCIFY
            -sALLOW_MEMORY_GROWTH
			-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']
    		--shell-file "${CMAKE_CURRENT_SOURCE_DIR}/src/${SHELL_FILE}"
        )
    endif()
	set_property(
		TARGET WebGPU_Engine
		PROPERTY LINK_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/src/${SHELL_FILE}"
	)
else()
    # Post-build: copy external DLLs
    file(GLOB_RECURSE EXTERNAL_DLLS
        "${CMAKE_CURRENT_BINARY_DIR}/external/*.dll"
    )
    foreach(DLL_FILE ${EXTERNAL_DLLS})
        get_filename_component(DLL_NAME ${DLL_FILE} NAME)
        add_custom_command(TARGET WebGPU_Engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DLL_FILE}"
                $<TARGET_FILE_DIR:WebGPU_Engine>/${DLL_NAME}
        )
    endforeach()
endif()

# Post-build: copy WebGPU binaries (defined in utils.cmake)
target_copy_webgpu_binaries(WebGPU_Engine)
