import platform
from conans import ConanFile, CMake, tools

class ODSConan(ConanFile):
    
    settings = "os", "compiler", "build_type", "arch"

    # comma-separated list of requirements
    requires = [
    "boost/1.80.0",
    "log4cplus/2.0.4",
    "nlohmann_json/3.9.1",
    "openssl/1.1.1k",
    "protobuf/3.17.1",
    "cryptopp/8.5.0",
    "libcurl/7.87.0"] 

    if platform.system() == "Windows":
        requires += ["dlfcn/1.0.0@prod/stable"]
    # elif platform.system() == "Linux":
    #     requires += ["libcurl/7.78.0"]

    generators = "cmake"
    default_options = {"boost:shared":False,
    "openssl:shared":False,
    "log4cplus:shared":False,
    "protobuf:shared":False,
    "cryptopp:shared":False
    }

def imports(self):
    self.copy("*.dll", dst="bin", src="bin") # From bin to bin
    self.copy("*.dylib*", dst="bin", src="lib") # From lib to bin