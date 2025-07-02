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

# Conan 2.0 integration
if(EXISTS "${CMAKE_BINARY_DIR}/generators/conan_toolchain.cmake")
    # Using Conan 2.0 - CMakeToolchain
    cmake_policy(SET CMP0091 NEW)
    include(${CMAKE_BINARY_DIR}/generators/conan_toolchain.cmake)
endif()

# We're using Boost.Asio instead of standalone Asio
option(USE_ASIO "Use standalone Asio instead of Boost.Asio" OFF)

option(BUILD_TESTS "Build tests" ON)
message(STATUS "BUILD_TESTS:  " ${BUILD_TESTS})

option(BUILD_PERF_TOOLS "Build Pulsar CLI perf producer/consumer" OFF)
message(STATUS "BUILD_PERF_TOOLS:  " ${BUILD_PERF_TOOLS})

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
    # Get Boost include directories and add them to the include path
    get_target_property(BOOST_INCLUDE_DIRS Boost::boost INTERFACE_INCLUDE_DIRECTORIES)
    include_directories(${BOOST_INCLUDE_DIRS})
    message(STATUS "Using Boost include dirs: ${BOOST_INCLUDE_DIRS}")
    
    find_package(OpenSSL REQUIRED)
    # Get OpenSSL include directories
    get_target_property(OPENSSL_INCLUDE_DIRS OpenSSL::SSL INTERFACE_INCLUDE_DIRECTORIES)
    include_directories(${OPENSSL_INCLUDE_DIRS})
    message(STATUS "Using OpenSSL include dirs: ${OPENSSL_INCLUDE_DIRS}")
    
    find_package(Protobuf REQUIRED)
    # Get Protobuf include directories
    get_target_property(PROTOBUF_INCLUDE_DIRS protobuf::libprotobuf INTERFACE_INCLUDE_DIRECTORIES)
    include_directories(${PROTOBUF_INCLUDE_DIRS})
    message(STATUS "Using Protobuf include dirs: ${PROTOBUF_INCLUDE_DIRS}")
    
    find_package(ZLIB REQUIRED)
    # Get ZLIB include directories
    get_target_property(ZLIB_INCLUDE_DIRS ZLIB::ZLIB INTERFACE_INCLUDE_DIRECTORIES)
    include_directories(${ZLIB_INCLUDE_DIRS})
    message(STATUS "Using ZLIB include dirs: ${ZLIB_INCLUDE_DIRS}")
    
    find_package(CURL REQUIRED)
    # Get CURL include directories
    get_target_property(CURL_INCLUDE_DIRS CURL::libcurl INTERFACE_INCLUDE_DIRECTORIES)
    include_directories(${CURL_INCLUDE_DIRS})
    message(STATUS "Using CURL include dirs: ${CURL_INCLUDE_DIRS}")
    
    find_package(Snappy REQUIRED)
    # Get Snappy include directories
    get_target_property(SNAPPY_INCLUDE_DIRS Snappy::snappy INTERFACE_INCLUDE_DIRECTORIES)
    include_directories(${SNAPPY_INCLUDE_DIRS})
    message(STATUS "Using Snappy include dirs: ${SNAPPY_INCLUDE_DIRS}")
    
    find_package(zstd REQUIRED)
    # Get zstd include directories
    get_target_property(ZSTD_INCLUDE_DIRS zstd::libzstd_static INTERFACE_INCLUDE_DIRECTORIES)
    include_directories(${ZSTD_INCLUDE_DIRS})
    message(STATUS "Using zstd include dirs: ${ZSTD_INCLUDE_DIRS}")
    
    find_package(nlohmann_json REQUIRED)
    # Get nlohmann_json include directories
    get_target_property(JSON_INCLUDE_DIRS nlohmann_json::nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)
    include_directories(${JSON_INCLUDE_DIRS})
    message(STATUS "Using nlohmann_json include dirs: ${JSON_INCLUDE_DIRS}")
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

# Add CURL_STATICLIB definition for Windows when linking statically
if(WIN32 AND NOT BUILD_SHARED_LIBS)
    add_definitions(-DCURL_STATICLIB)
endif()

set(AUTOGEN_DIR ${PROJECT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${AUTOGEN_DIR})

# Set up protoc path for code generation
if(EXISTS "${CMAKE_BINARY_DIR}/generators/conan_toolchain.cmake")
    # When using Conan 2, protobuf::protoc target should be available after find_package(Protobuf)
    # and Protobuf_PROTOC_EXECUTABLE should be set
    if(TARGET protobuf::protoc)
        set(PROTOC_PATH "${Protobuf_PROTOC_EXECUTABLE}")
        message(STATUS "Using protoc from Conan target: ${PROTOC_PATH}")
    else()
        # Fallback to manual search if target not available
        find_program(PROTOC_PATH protoc)
        if(NOT PROTOC_PATH)
            message(FATAL_ERROR "Could not find protoc executable. Please ensure protobuf is properly installed.")
        endif()
        message(STATUS "Using protoc (fallback): ${PROTOC_PATH}")
    endif()
else()
    # Fallback to system protoc or previously defined path
    if(NOT PROTOC_PATH)
        find_program(PROTOC_PATH protoc)
        if(NOT PROTOC_PATH)
            message(FATAL_ERROR "Could not find protoc executable. Please install protobuf or specify PROTOC_PATH.")
        endif()
    endif()
    message(STATUS "Using protoc: ${PROTOC_PATH}")
endif()

# Define common libraries for linking
if(EXISTS "${CMAKE_BINARY_DIR}/generators/conan_toolchain.cmake")
    # Using Conan-provided dependencies
    set(COMMON_LIBS 
        CURL::libcurl
        ZLIB::ZLIB
        OpenSSL::SSL
        OpenSSL::Crypto
        protobuf::libprotobuf
        zstd::libzstd_static
        Snappy::snappy
        nlohmann_json::nlohmann_json
    )
    
    # We're using Boost.Asio instead of standalone Asio
    if (USE_ASIO)
        message(FATAL_ERROR "Standalone Asio is not supported in this build. Please set USE_ASIO=OFF to use Boost.Asio instead.")
    else()
        # Using Boost.Asio
        message(STATUS "Using Boost.Asio")
    endif()
    
    # Add Windows-specific libraries
    if (WIN32)
        # Add dlfcn-win32 for Windows dynamic library loading
        find_package(dlfcn-win32 REQUIRED)
        get_target_property(DLFCN_WIN32_INCLUDE_DIRS dlfcn-win32::dlfcn-win32 INTERFACE_INCLUDE_DIRECTORIES)
        include_directories(${DLFCN_WIN32_INCLUDE_DIRS})
        list(APPEND COMMON_LIBS dlfcn-win32::dlfcn-win32)
        # Add definition to indicate we're using dlfcn-win32
        add_definitions(-DHAS_DLFCN_WIN32)
        
        # Create a variable without dashes for compatibility with legacy code
        set(DLFCN_WIN32_LIBRARY dlfcn-win32::dlfcn-win32 CACHE STRING "dlfcn-win32 library target" FORCE)
        message(STATUS "Using dlfcn-win32 from Conan: ${DLFCN_WIN32_INCLUDE_DIRS}")
    endif()
    
    # Add Boost libraries
    list(APPEND COMMON_LIBS Boost::boost)
    if (BUILD_PERF_TOOLS)
        list(APPEND COMMON_LIBS Boost::program_options)
    endif()
else()
    # Fallback to legacy find packages if not using Conan
    include(./LegacyFindPackages.cmake)
endif()

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
