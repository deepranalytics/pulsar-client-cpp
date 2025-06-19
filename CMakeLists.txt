#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#

cmake_minimum_required(VERSION 3.13)

option(USE_ASIO "Use Asio instead of Boost.Asio" OFF)

option(INTEGRATE_VCPKG "Integrate with Vcpkg" OFF)
if (INTEGRATE_VCPKG)
    set(USE_ASIO ON)
    if (NOT CMAKE_TOOLCHAIN_FILE)
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
    endif ()
endif ()

option(BUILD_TESTS "Build tests" ON)
message(STATUS "BUILD_TESTS:  " ${BUILD_TESTS})
if (BUILD_TESTS)
    list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif ()

option(BUILD_PERF_TOOLS "Build Pulsar CLI perf producer/consumer" OFF)
message(STATUS "BUILD_PERF_TOOLS:  " ${BUILD_PERF_TOOLS})
if (BUILD_PERF_TOOLS)
    list(APPEND VCPKG_MANIFEST_FEATURES "perf")
endif ()

add_definitions(-DCMS_MAJOR_VER=5)
add_definitions(-DCMS_MINOR_VER=0)
add_definitions(-DCMS_REVISION_VER=0)

message("current list: ${CMAKE_CURRENT_LIST_DIR}")
project(pulsar)
#set(CMAKE_INSTALL_PREFIX ${CMAKE_CURRENT_LIST_DIR}/x64/unix/${CMAKE_BUILD_TYPE})
#set(CMAKE_INSTALL_PREFIX ${CMAKE_CURRENT_LIST_DIR}/x64/windows/${CMAKE_BUILD_TYPE})

# Enable cpp 17 standard.
set(CMAKE_CXX_STANDARD 17)

# Conan 2.0 integration
if(EXISTS "${CMAKE_BINARY_DIR}/generators/conan_toolchain.cmake")
    # Using Conan 2.0 - CMakeToolchain
    cmake_policy(SET CMP0091 NEW)
    include(${CMAKE_BINARY_DIR}/generators/conan_toolchain.cmake)
    # Find dependencies
    find_package(Boost REQUIRED)
    find_package(OpenSSL REQUIRED)
    find_package(Protobuf REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(CURL REQUIRED)
    find_package(nlohmann_json REQUIRED)
    find_package(log4cplus REQUIRED)
    find_package(cryptopp REQUIRED)
    if(WIN32)
        find_package(dlfcn-win32 REQUIRED)
    endif()
endif()


# Check current platform architecture.
if (${CMAKE_SIZEOF_VOID_P} EQUAL "8")
    set (CMS_ARCHITECTURE "x64")
else ()
    set (CMS_ARCHITECTURE "x86")
endif ()

# # Create directory where CMSAPI will be copied on installation.
# # For windows it will be project_dir/x64/windows/{Debug/Release}
# # For Linux it will be project_dir/x64/unix/{Debug/Release}
if(CONAN_EXPORTED) # in conan local cache
    set(CMS_DESTINATION_DIR "${CMAKE_BINARY_DIR}/${CMS_ARCHITECTURE}/${CURRENT_SYSTEM_TYPE}/${CMAKE_BUILD_TYPE}")
else()
    set(CMS_DESTINATION_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${CMS_ARCHITECTURE}/${CURRENT_SYSTEM_TYPE}/${CMAKE_BUILD_TYPE}")
endif()

message("destination: ${CMS_DESTINATION_DIR}")
file(MAKE_DIRECTORY ${CMS_DESTINATION_DIR})

if(DEFINED ${CONAN_INSTALL_PREFIX})
    message("Conan Install Prefix Is Given: ${CONAN_INSTALL_PREFIX}")
    set(CMS_INSTALL_DIR ${CONAN_INSTALL_PREFIX})
else()
    message("Building with default install path.")
    set(CMS_INSTALL_DIR "${CMS_DESTINATION_DIR}/../install")
    file(MAKE_DIRECTORY ${CMS_INSTALL_DIR})
endif()

message("install: ${CMS_INSTALL_DIR}")
set(CMS_PARENT_PATH "${CMAKE_CURRENT_LIST_DIR}")

if(WIN32)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMS_DESTINATION_DIR})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMS_DESTINATION_DIR})
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMS_DESTINATION_DIR})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMS_DESTINATION_DIR})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMS_DESTINATION_DIR})
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMS_DESTINATION_DIR})
elseif(UNIX)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMS_DESTINATION_DIR})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMS_DESTINATION_DIR})
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMS_DESTINATION_DIR})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMS_DESTINATION_DIR})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMS_DESTINATION_DIR})
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMS_DESTINATION_DIR})
endif()


















set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake_modules")
set(PROTOC_PATH "${CONAN_BIN_DIRS_PROTOBUF}/protoc")

execute_process(COMMAND cat ${PROJECT_SOURCE_DIR}/version.txt OUTPUT_STRIP_TRAILING_WHITESPACE
            OUTPUT_VARIABLE PULSAR_CLIENT_VERSION)
message(STATUS "Pulsar Client version: ${PULSAR_CLIENT_VERSION}")

execute_process(COMMAND ${PROJECT_SOURCE_DIR}/build-support/gen-pulsar-version-macro.py OUTPUT_STRIP_TRAILING_WHITESPACE
        OUTPUT_VARIABLE PULSAR_CLIENT_VERSION_MACRO)
message(STATUS "Pulsar Client version macro: ${PULSAR_CLIENT_VERSION_MACRO}")

set(PVM_COMMENT "This is generated from Version.h.in by CMAKE. DO NOT EDIT DIRECTLY")
configure_file(templates/Version.h.in include/pulsar/Version.h @ONLY)

option(LINK_STATIC "Link against static libraries" OFF)
if (VCPKG_TRIPLET)
    message(STATUS "Use vcpkg, triplet is ${VCPKG_TRIPLET}")
    set(CMAKE_PREFIX_PATH "${PROJECT_SOURCE_DIR}/vcpkg_installed/${VCPKG_TRIPLET}")
    message(STATUS "Use CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")
    set(PROTOC_PATH "${CMAKE_PREFIX_PATH}/tools/protobuf/protoc")
    message(STATUS "Use protoc: ${PROTOC_PATH}")
    set(VCPKG_DEBUG_ROOT "${PROJECT_SOURCE_DIR}/vcpkg_installed/${VCPKG_TRIPLET}/debug")
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(ZLIB_ROOT ${VCPKG_DEBUG_ROOT})
        set(OPENSSL_ROOT_DIR ${VCPKG_DEBUG_ROOT})
        set(CMAKE_PREFIX_PATH ${VCPKG_DEBUG_ROOT} ${CMAKE_PREFIX_PATH})
    endif ()
    if (VCPKG_TRIPLET MATCHES ".*-static")
        set(LINK_STATIC ON)
    else ()
        set(LINK_STATIC OFF)
    endif ()
endif()
MESSAGE(STATUS "LINK_STATIC:  " ${LINK_STATIC})

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "ccache")
    MESSAGE(STATUS "Using CCache")
endif(CCACHE_PROGRAM)

MESSAGE(STATUS "ARCHITECTURE: ${CMAKE_SYSTEM_PROCESSOR}")

option(BUILD_DYNAMIC_LIB "Build dynamic lib" ON)
MESSAGE(STATUS "BUILD_DYNAMIC_LIB:  " ${BUILD_DYNAMIC_LIB})

option(BUILD_STATIC_LIB "Build static lib" ON)
MESSAGE(STATUS "BUILD_STATIC_LIB:  " ${BUILD_STATIC_LIB})

option(BUILD_TESTS "Build tests" ON)
MESSAGE(STATUS "BUILD_TESTS:  " ${BUILD_TESTS})

option(BUILD_PERF_TOOLS "Build Pulsar CLI perf producer/consumer" OFF)
MESSAGE(STATUS "BUILD_PERF_TOOLS:  " ${BUILD_PERF_TOOLS})

option(USE_LOG4CXX "Build with Log4cxx support" OFF)
MESSAGE(STATUS "USE_LOG4CXX:  " ${USE_LOG4CXX})

IF (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
ENDIF ()

MESSAGE(STATUS "CMAKE_BUILD_TYPE:  " ${CMAKE_BUILD_TYPE})

set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)
MESSAGE(STATUS "Threads library: " ${CMAKE_THREAD_LIBS_INIT})

# Compiler specific configuration:
# https://stackoverflow.com/questions/10046114/in-cmake-how-can-i-test-if-the-compiler-is-clang
if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOGDI -D_WIN32_WINNT=0x0501 -D_CRT_SECURE_NO_WARNINGS)
    add_compile_options(/wd4244 /wd4267 /wd4018 /wd4715 /wd4251 /wd4275 /wd4819)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Intel")
    # ?? Don't have this to test with
else() # GCC or Clang are mostly compatible:
    # Turn on warnings and enable warnings-as-errors:
    add_compile_options(-Wall -Wformat-security -Wvla -Werror) 
    # Turn off certain warnings that are too much pain for too little gain:
    add_compile_options(-Wno-sign-compare -Wno-deprecated-declarations -Wno-error=cpp)
    if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR APPLE)
        add_compile_options(-msse4.2 -mpclmul)
    endif()
    # Options unique to Clang or GCC:
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-Qunused-arguments) 
    endif()
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_definitions(-DBUILDING_PULSAR -DBOOST_ALL_NO_LIB -DBOOST_ALLOW_DEPRECATED_HEADERS)

set(AUTOGEN_DIR ${PROJECT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${AUTOGEN_DIR})

if (INTEGRATE_VCPKG)
    if (NOT CMAKE_CXX_STANDARD)
        set(CMAKE_CXX_STANDARD 11)
    endif ()
    set(CMAKE_C_STANDARD 11)
    set(Boost_NO_BOOST_CMAKE ON)
    find_package(Boost REQUIRED)
    include_directories(${Boost_INCLUDE_DIRS})
    message("Boost_INCLUDE_DIRS: ${Boost_INCLUDE_DIRS}")

    set(CURL_NO_CURL_CMAKE ON)
    find_package(CURL REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(OpenSSL REQUIRED)
    find_package(protobuf CONFIG REQUIRED)
    find_package(zstd CONFIG REQUIRED)
    find_package(Snappy CONFIG REQUIRED)
    set(COMMON_LIBS CURL::libcurl
        ZLIB::ZLIB
        OpenSSL::SSL
        OpenSSL::Crypto
        protobuf::libprotobuf
        $<IF:$<TARGET_EXISTS:zstd::libzstd_shared>,zstd::libzstd_shared,zstd::libzstd_static>
        Snappy::snappy
        )
    if (USE_ASIO)
        find_package(asio CONFIG REQUIRED)
        set(COMMON_LIBS ${COMMON_LIBS} asio::asio)
    endif ()
    add_definitions(-DHAS_ZSTD -DHAS_SNAPPY)
    if (MSVC)
        find_package(dlfcn-win32 CONFIG REQUIRED)
    endif ()
    if (BUILD_PERF_TOOLS)
        find_package(Boost COMPONENTS program_options REQUIRED)
    endif ()
else ()
    include(./LegacyFindPackages.cmake)
endif ()

if (USE_ASIO)
    add_definitions(-DUSE_ASIO)
endif ()

set(LIB_NAME $ENV{PULSAR_LIBRARY_NAME})
if (NOT LIB_NAME)
    set(LIB_NAME pulsar)
endif(NOT LIB_NAME)

set(CLIENT_LIBS
  ${COMMON_LIBS}
  ${LIB_NAME}
)

add_subdirectory(lib)
if(BUILD_PERF_TOOLS)
    add_subdirectory(perf)
endif(BUILD_PERF_TOOLS)

if (BUILD_DYNAMIC_LIB)
    #add_subdirectory(examples)
endif()

if (BUILD_TESTS)
    set(TOKEN_PATH "${PROJECT_SOURCE_DIR}/.test-token.txt")
    set(TEST_CONF_DIR "${PROJECT_SOURCE_DIR}/test-conf")
    add_subdirectory(tests)
endif()

if (BUILD_WIRESHARK)
    add_subdirectory(wireshark)
endif()

find_package(ClangTools)
set(BUILD_SUPPORT_DIR "${PROJECT_SOURCE_DIR}/build-support")
add_custom_target(format ${BUILD_SUPPORT_DIR}/run_clang_format.py
        ${CLANG_FORMAT_BIN}
        0
        ${BUILD_SUPPORT_DIR}/clang_format_exclusions.txt
        ${PROJECT_SOURCE_DIR}/lib
        ${PROJECT_SOURCE_DIR}/perf
        ${PROJECT_SOURCE_DIR}/examples
        ${PROJECT_SOURCE_DIR}/tests
        ${PROJECT_SOURCE_DIR}/tests/unix
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/wireshark)

# `make check-format` option (for CI test)
add_custom_target(check-format ${BUILD_SUPPORT_DIR}/run_clang_format.py
        ${CLANG_FORMAT_BIN}
        1
        ${BUILD_SUPPORT_DIR}/clang_format_exclusions.txt
        ${PROJECT_SOURCE_DIR}/lib
        ${PROJECT_SOURCE_DIR}/perf
        ${PROJECT_SOURCE_DIR}/examples
        ${PROJECT_SOURCE_DIR}/tests
        ${PROJECT_SOURCE_DIR}/tests/unix
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/wireshark)
