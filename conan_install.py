import os
import platform
import subprocess
from pathlib import Path
from enum import IntEnum
import argparse
import sys

class BuildType(IntEnum):
    DEBUG = 1
    RELEASE = 2

def main(build_type):
    system = platform.system()
    print(f"Running Conan 2 install script for {system} platform.")
    
    # Create build directory if it doesn't exist
    build_dir = "build"
    if not os.path.exists(build_dir):
        os.mkdir(build_dir)
    
    # Change to build directory
    os.chdir(build_dir)
    
    # Base command for both platforms
    base_command = [
        "conan", "install", "..",
        "--build=missing"
    ]
    
    # Add platform and build type specific options
    if system == "Windows":
        if build_type == BuildType.DEBUG:
            command = base_command + [
                "-s", "build_type=Debug",
                "-s", "compiler.runtime=dynamic"
            ]
        else:
            command = base_command + [
                "-s", "build_type=Release",
                "-s", "compiler.runtime=dynamic"
            ]
    elif system == "Linux":
        if build_type == BuildType.DEBUG:
            command = base_command + [
                "-s", "build_type=Debug"
            ]
        else:
            command = base_command + [
                "-s", "build_type=Release"
            ]
    else:
        print(f"Unknown system platform {system}.")
        return 1
    
    # Run the command
    print(f"Running command: {' '.join(command)}")
    result = subprocess.run(command)
    
    # Return to original directory
    os.chdir("..")
    
    return result.returncode

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Conan 2 install script')
    parser.add_argument('-m', '--mode', type=str, help='compile mode.\nAllowed modes are \n -> release \n -> debug')
    
    args = parser.parse_args()

    if len(sys.argv) < 2:
        parser.print_help()
        sys.exit(1)

    build_mode = args.mode
    build_type = -1
    if build_mode.upper() == 'DEBUG':
        build_type = BuildType.DEBUG
    elif build_mode.upper() == 'RELEASE':
        build_type = BuildType.RELEASE

    if build_type != BuildType.DEBUG and build_type != BuildType.RELEASE:
        print(f"Invalid input '{build_mode}'. Enter debug or release")
        exit(1)

    print(f"TYPE SELECTED: {build_mode}")
    ret_code = main(build_type)
    print(f"INSTALL COMMAND FINISHED WITH CODE {ret_code}")
