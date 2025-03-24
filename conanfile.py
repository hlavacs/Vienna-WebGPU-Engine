from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps
from conan.tools.files import copy
import os

class WebGPUEngineConan(ConanFile):
    name = "WebGPU_Engine"
    version = "0.1.0"
    settings = "os", "arch", "compiler", "build_type"
    generators = "CMakeDeps", "CMakeToolchain"

    requires = [
        "glm/0.9.9.8",
        "sdl/2.28.3"
    ]
    exports_sources = "src/*", "CMakeLists.txt", "external/*"

    def layout(self):
        self.folders.build = "build"
        self.folders.generators = os.path.join(self.folders.build, "generators")
    
    def package(self):
        copy(self, "*.h", src=self.source_folder, dst=os.path.join(self.package_folder, "include"))
        copy(self, "*.lib", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"))
        copy(self, "*.a", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"))
        copy(self, "*.dll", src=self.build_folder, dst=os.path.join(self.package_folder, "bin"))
        copy(self, "*.so*", src=self.build_folder, dst=os.path.join(self.package_folder, "bin"))
        copy(self, "*.dylib", src=self.build_folder, dst=os.path.join(self.package_folder, "bin"))
