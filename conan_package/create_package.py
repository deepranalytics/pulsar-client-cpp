import os
import platform
import subprocess

# Either dev, prod, etc.
g_maturity = "prod"
# Either (alpha, beta, etc.)
g_release_type = "test"
g_release_version = "3.1.1"

class bcolors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'

def printFooter():
    print(
        f"""
        \r{bcolors.OKGREEN}Building process is finished.{bcolors.ENDC}
        \r{bcolors.OKCYAN}Upload package with following commands.{bcolors.ENDC}
        \r{bcolors.OKCYAN}Conan publish commands:{bcolors.ENDC}
        \rGenerate below command from "Set Me Up" tab in {bcolors.OKBLUE}https://deeprdev.jfrog.io{bcolors.ENDC}
        \r$ {bcolors.OKGREEN}conan{bcolors.ENDC} remote add <remote_name> {bcolors.OKBLUE}https://deeprdev.jfrog.io/artifactory/api/conan/deepr-conan{bcolors.ENDC}
        \r$ {bcolors.OKGREEN}conan{bcolors.ENDC} user -p <password> -r <remote_name> <username>
        \r$ {bcolors.OKGREEN}conan{bcolors.ENDC} upload pulsar/{g_release_version}@{g_maturity}/{g_release_type} -r <remote_name> --all --force
        """
    )

def main():
    print('current working directory' + os.getcwd())
    system = platform.system()
    print(f"{bcolors.HEADER}Running python script to build package on %s platform.{bcolors.ENDC}" % (system))

    commands = []

    if system == "Windows":
        commands.append(
            [
                "conan", "create"
                , "."
                , ("%s/%s" % (g_maturity, g_release_type))
                , "-u"
                , "--profile=../winprofile.txt"
                , "--build=missing"
                , "-s", "build_type=Debug"
                , "-s", "compiler.runtime=MDd"
            ]
        )
        commands.append(
            [
                "conan", "create"
                , "."
                , ("%s/%s" % (g_maturity, g_release_type))
                , "-u"
                , "--profile=../winprofile.txt"
                , "--build=missing"
                , "-s", "build_type=Release"
                , "-s", "compiler.runtime=MD"
            ]
        )
    elif system == "Linux":
        commands.append(
            [
                "conan", "create"
                , "."
                , ("%s/%s" % (g_maturity, g_release_type))
                , "-u"
                , "--profile=../linuxprofile.txt"
                , "--build=missing"
                , "-s", "build_type=Debug"
            ]
        )
        commands.append(
            [
                "conan", "create"
                , "."
                , ("%s/%s" % (g_maturity, g_release_type))
                , "-u"
                , "--profile=../linuxprofile.txt"
                , "--build=missing"
                , "-s", "build_type=Release"
            ]
        )
    else:
        print(f"{bcolors.FAIL}Unknown system platform %s.{bcolors.ENDC}" % (system))
        return 1

    for command in commands:
        print("conan command: ",command)
        subprocess.run(command)
    return 0

if __name__ == "__main__":
    ret_code = main()
    
    if ret_code == 0:
        printFooter()