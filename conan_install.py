import os
import platform
import subprocess
from pathlib import Path
from enum import IntEnum
import argparse
import sys

conan_install_dir = "conan"

class BuildType(IntEnum):
    DEBUG = 1
    RELEASE = 2

def prepare_install_dir(main_dir):
    main_path = Path(main_dir)
    install_dir = Path(main_path).joinpath(conan_install_dir)
    if not os.path.exists(install_dir):
        os.mkdir(install_dir)
    return install_dir

def main(build_type):
    system = platform.system()
    print("Running python script to build package on %s platform." % (system))
    commands = []

    if system == "Windows":
        if build_type == BuildType.DEBUG:
            commands.append(
                [
                    "conan"
                    , "install"
                    , ".."
                    , "-u"
                    , "--profile=../winprofile.txt"
                    , "--build=missing"
                    , "-s", "build_type=Debug"
                    , "-s", "compiler.runtime=MDd"
                    , "--no-imports"
                ]
            )
        else:
            commands.append(
                [
                    "conan"
                    , "install"
                    , ".."
                    , "-u"
                    , "--profile=../winprofile.txt"
                    , "--build=missing"
                    , "-s", "build_type=Release"
                    , "-s", "compiler.runtime=MD"
                    , "--no-imports"
                ]
            )
    elif system == "Linux":
        if build_type == BuildType.DEBUG:
            commands.append(
                [
                    "conan"
                    , "install"
                    , ".."
                    , "-u"
                    , "--profile=../linuxprofile.txt"
                    , "--build=missing"
                    , "-s", "build_type=Debug"
                    , "--no-imports"
                ]
            )
        else:
            commands.append(
                [
                    "conan"
                    , "install"
                    , ".."
                    , "-u"
                    , "--profile=../linuxprofile.txt"
                    , "--build=missing"
                    , "-s", "build_type=Release"
                    , "--no-imports"
                ]
            )
    else:
        print(f"Unknown system platform %s." % (system))
        return 1

    main_dir = os.getcwd()
    install_dir = prepare_install_dir(main_dir)
    os.chdir(install_dir)

    for command in commands:
        print("Running command: %s" % (" ".join(command)))
        subprocess.run(command)

    os.chdir(main_dir)
    return 0

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='--mode')
    parser.add_argument('-m', '--mode', type=str, help='compile mode.\nAllowed modes are \n -> release \n -> debug')
    
    args = parser.parse_args()

    if len(sys.argv) < 2:
        parser.print_help()
        sys.exit()

    build_mode = args.mode
    build_type = -1
    if build_mode.upper() == 'DEBUG':
        build_type = BuildType.DEBUG
    elif build_mode.upper() == 'RELEASE':
        build_type = BuildType.RELEASE

    if build_type != BuildType.DEBUG and build_type != BuildType.RELEASE:
        print("{Invalid input '%s'} Enter debug or release" % (build_mode))
        exit(1)

    print("TYPE SELECTED: %s" % (build_mode))
    ret_code = main(build_type)
    print("INSTALL COMMAND FINISHED WITH CODE %d" % ret_code)