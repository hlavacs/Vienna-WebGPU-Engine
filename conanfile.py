from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout
from conan.tools.files import copy
import os

class WebGPUEngineConan(ConanFile):
    name = "WebGPU_Engine"
    version = "0.1.0"
    author = "Kalian Danzer"
    license = "MIT License"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps"

    def requirements(self):
        if self.settings.os == "Emscripten":
            # When building for Emscripten, use the appropriate dependencies
            self.requires("sdl/2.28.3")  # Emscripten SDL2
            self.requires("glm/0.9.9.8")  # For Emscripten
        else:
            # For other OS, use regular dependencies
            self.requires("sdl/2.28.3")
            self.requires("glm/0.9.9.8")

    def layout(self):
        if self.settings.os == "Emscripten":
            cmake_layout(self, build_folder="build-web")
        else:
            cmake_layout(self)

    def package(self):
        copy(self, "*.h", src=self.source_folder, dst=os.path.join(self.package_folder, "include"))
        copy(self, "*.lib", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"))
        copy(self, "*.a", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"))
        copy(self, "*.dll", src=self.build_folder, dst=os.path.join(self.package_folder, "bin"))
        copy(self, "*.so*", src=self.build_folder, dst=os.path.join(self.package_folder, "bin"))
        copy(self, "*.dylib", src=self.build_folder, dst=os.path.join(self.package_folder, "bin"))
