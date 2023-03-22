__author__ = "Karan Kumar"

import os
import shutil

from conans import ConanFile, CMake, tools
from pathlib import Path

# Explicitly downloading conan packages and setup build directory.
# Reference for conanfile.py: https://github.com/conan-io/conan/issues/5948
class CMSConan(ConanFile):
    name = "pulsar"
    version = "3.1.1"
    license = "<DeepR proprietary license>"
    author = "<Lalit Gangwar> <lalit.gangwar@equtick.com>"
    # url = "<Package recipe repository url here, for issues about the package>"
    # https://docs.conan.io/en/latest/creating_packages/package_repo.html#capturing-the-remote-and-commit-scm
    # If using '.' as subfolder then 'conan create' will use current project directory as source dir
    # without copying source to build folder which might take some time.
    # After subfolder, other fields should be auto in this case. 
    # Note: For uploading '--force' CLA should be given with upload command.
    scm = {
        "type": "git",  # Use "type": "svn", if local repo is managed using SVN
        "subfolder": ".",
        "url": "auto",
        "revision": "auto"
    }
    description = "Apache Pulsar"
    topics = ("CMS", "C++")
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [False]
        }
    
    default_options = {
        "shared": False, 
        "boost:shared":False,
        "log4cplus:shared":False,
        "openssl:shared":False,
        "protobuf:shared":False,
        "cryptopp:shared":False
        }
    generators = "cmake" #, "cmake_find_package"
    no_copy_source = True

    # custom vars.
    _build_dir = "build"
    _conan_dir = "conan"

    def source(self):
        pass

    def requirements(self):
        print("<< REQUIREMENTS BEGIN >>")
        requirements = [
            "boost/1.80.0",
            "log4cplus/2.0.4",
            "nlohmann_json/3.9.1",
            "openssl/1.1.1k",
            "protobuf/3.17.1",
            "cryptopp/8.5.0",
            "libcurl/7.87.0"
        ]

        if self.settings.os == "Windows":
           requirements += ["dlfcn/1.0.0@prod/stable"]

        for deps in requirements:
            self.requires(deps)
        print("<< REQUIREMENTS END >>")

    def _configure_cmake(self, cmake_build_type):
        print("SourceFolder: " + self.source_folder)
        print("BuildDir: " + self._build_dir)
        cmake = CMake(self, build_type=cmake_build_type)
        cmake.configure(build_folder=self._build_dir, source_folder=self.source_folder)
        return cmake

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC
            self.current_system = "windows"
        elif self.settings.os == "Linux":
            self.current_system = "unix"
            # Do some linux configuration stuff here.
            pass

    def build(self):
        self.cmake = self._configure_cmake(self.settings.build_type)
        self.cmake.build()

    def package(self):
        # Install with cmake and then use conan copy commands to copy headers and libs.
        self.cmake.install()

        src_install_dir = self._build_dir + "/x64/%s/install" % (self.current_system)
        
        print("Source Install Dir: " + src_install_dir)
        self.copy("*.h", dst="include", src=src_install_dir + "/include")
        self.copy("*.hpp", dst="include", src=src_install_dir + "/include")
        # configs = ["Debug", "Release"]
        configs = [self.settings.build_type]
        for config in configs:
            src_build_dir = self._build_dir + "/x64/%s/%s/" % (self.current_system, config)
            print("Source Build Dir: " + src_build_dir)
            self.copy("*.lib", dst="lib", src=src_build_dir)
            self.copy("*.dll", dst="bin", src=src_build_dir)
            self.copy("*.dylib*", dst="lib", src=src_build_dir)
            self.copy("*.so", dst="lib", src=src_build_dir)
            self.copy("*.a", dst="lib", src=src_build_dir)

    def package_info(self):
        self.cpp_info.release.libs = ["pulsar"]
        self.cpp_info.debug.libs = ["pulsar"]