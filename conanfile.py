from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps
from conan.tools.files import copy
import os

class PulsarConan(ConanFile):
    name = "pulsar"
    version = "3.7.1"  # Updated to 3.7.1 as requested
    license = "Apache-2.0"
    author = "DeepR Analytics"
    description = "Apache Pulsar C++ client library"
    topics = ("pulsar", "messaging", "pubsub")
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [False]
    }
    default_options = {
        "shared": False,
        "boost/*:shared": False,
        "openssl/*:shared": False,
        "protobuf/*:shared": False,
        "zlib/*:shared": False,
        "curl/*:shared": False
    }
    
    # For SCM, using auto values which will be filled by Conan
    scm = {
        "type": "git",
        "subfolder": ".",
        "url": "auto",
        "revision": "auto"
    }
    
    # Set this to True to avoid copying source files
    no_copy_source = True
    exports_sources = "*"

    def requirements(self):
        # Dependencies from vcpkg.json
        self.requires("boost/1.83.0")  # Using boost version from vcpkg
        self.requires("openssl/3.5.0")  # Updated to match vcpkg
        self.requires("protobuf/3.21.12")  # Updated to match vcpkg
        self.requires("zlib/1.3.1")  # Updated to match vcpkg
        self.requires("libcurl/8.12.1")  # Updated to match vcpkg
        self.requires("snappy/1.1.10")  # Added from vcpkg
        self.requires("zstd/1.5.5")  # Added from vcpkg
        self.requires("nlohmann_json/3.9.1")
        
        if self.settings.os == "Windows":
            self.requires("dlfcn-win32/1.4.1")
    
    def layout(self):
        # Define the layout for build, generators, etc.
        self.folders.source = "."
        self.folders.build = "build"
        self.folders.generators = os.path.join("build", "generators")
    
    def source(self):
        # This method is called when building the package in the cache
        # The SCM feature will clone the git repo into the cache
        # If using exports_sources, files will be copied to the cache
        pass
    
    def generate(self):
        # Generate CMake files
        deps = CMakeDeps(self)
        deps.generate()
        
        tc = CMakeToolchain(self)
        # Add any custom CMake variables here
        tc.variables["BUILD_TESTS"] = "OFF"
        tc.variables["BUILD_WIRESHARK"] = "OFF"
        tc.variables["BUILD_PERF_TOOLS"] = "OFF"
        tc.variables["USE_LOG4CXX"] = "OFF"
        tc.variables["HAS_SNAPPY"] = "1"  # Enable Snappy compression
        tc.variables["HAS_ZSTD"] = "1"  # Enable Zstd compression
        tc.variables["USE_CLANG_TOOLS"] = "OFF"  # Disable ClangTools requirement
        tc.generate()
    
    def build(self):
        cmake = CMake(self)
        # With no_copy_source=True, CMake should automatically find the source files
        cmake.configure()
        cmake.build()
    
    def package(self):
        cmake = CMake(self)
        cmake.install()
        
        # Copy headers - recursive to include subdirectories
        copy(self, "**/*.h", src=os.path.join(self.source_folder, "include"), 
             dst=os.path.join(self.package_folder, "include"), keep_path=True)
        
        # Copy libraries from Visual Studio output directory
        if self.settings.os == "Windows":
            # For Visual Studio builds, artifacts are in x64/Release
            copy(self, "*.lib", src=os.path.join(self.source_folder, "x64", "Release"), 
                 dst=os.path.join(self.package_folder, "lib"), keep_path=False)
            copy(self, "*.dll", src=os.path.join(self.source_folder, "x64", "Release"), 
                 dst=os.path.join(self.package_folder, "bin"), keep_path=False)
        
        # Copy libraries from build folder and its subdirectories
        copy(self, "*.lib", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.dll", src=self.build_folder, dst=os.path.join(self.package_folder, "bin"), keep_path=False)
        copy(self, "*.so", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.dylib", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.a", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        
        # Ensure we have the static library
        if self.settings.os == "Windows":
            copy(self, "pulsar-static.lib", src=os.path.join(self.source_folder, "x64", "Release"), 
                 dst=os.path.join(self.package_folder, "lib"), keep_path=False)
    
    def package_info(self):
        self.cpp_info.libs = ["pulsar"]
