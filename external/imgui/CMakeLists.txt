# Define an ImGui target that fits our use case
add_library(imgui STATIC
	# Among the different backends available, we are interested in connecting
	# the GUI to GLFW andWebGPU:
	backends/imgui_impl_wgpu.h
	backends/imgui_impl_wgpu.cpp
	backends/imgui_impl_sdl2.h
	backends/imgui_impl_sdl2.cpp

	# Bonus to add some C++ specific features (the core ImGUi is a C library)
	misc/cpp/imgui_stdlib.h
	misc/cpp/imgui_stdlib.cpp

	# The core ImGui files
	imconfig.h
	imgui.h
	imgui.cpp
	imgui_draw.cpp
	imgui_internal.h
	imgui_tables.cpp
	imgui_widgets.cpp
	imstb_rectpack.h
	imstb_textedit.h
	imstb_truetype.h
)

target_include_directories(imgui PUBLIC .)
target_link_libraries(imgui PUBLIC webgpu SDL2::SDL2)
