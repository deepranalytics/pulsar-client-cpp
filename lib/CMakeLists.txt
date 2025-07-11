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

file(GLOB PULSAR_SOURCES *.cc *.h lz4/*.cc lz4/*.h checksum/*.cc checksum/*.h stats/*.cc stats/*.h c/*.cc c/*.h auth/*.cc auth/*.h auth/athenz/*.cc auth/athenz/*.h)

if (NOT PROTOC_PATH)
    set(PROTOC_PATH protoc)
endif()

set(LIB_AUTOGEN_DIR ${AUTOGEN_DIR}/lib)
file(MAKE_DIRECTORY ${LIB_AUTOGEN_DIR})
include_directories(${LIB_AUTOGEN_DIR})

if (INTEGRATE_VCPKG)
    add_library(PROTO_OBJECTS OBJECT "${CMAKE_SOURCE_DIR}/proto/PulsarApi.proto")
    target_link_libraries(PROTO_OBJECTS PUBLIC protobuf::libprotobuf)
    target_include_directories(PROTO_OBJECTS PUBLIC ${LIB_AUTOGEN_DIR})
    protobuf_generate(
        TARGET PROTO_OBJECTS
        IMPORT_DIRS ${CMAKE_SOURCE_DIR}/proto
        PROTOC_OUT_DIR ${LIB_AUTOGEN_DIR})
    set(COMMON_LIBS ${COMMON_LIBS} PROTO_OBJECTS)
else ()
    set(PROTO_SOURCES ${LIB_AUTOGEN_DIR}/PulsarApi.pb.cc ${LIB_AUTOGEN_DIR}/PulsarApi.pb.h)
    set(PULSAR_SOURCES ${PULSAR_SOURCES} ${PROTO_SOURCES})
    ADD_CUSTOM_COMMAND(
             OUTPUT ${PROTO_SOURCES}
             COMMAND ${PROTOC_PATH} -I ../proto ../proto/PulsarApi.proto --cpp_out=${LIB_AUTOGEN_DIR}
             DEPENDS
             ../proto/PulsarApi.proto
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif ()

set(LIBRARY_VERSION $ENV{PULSAR_LIBRARY_VERSION})
if (NOT LIBRARY_VERSION)
    set(LIBRARY_VERSION ${PV})
endif(NOT LIBRARY_VERSION)

if (MSVC)
    set(CMAKE_DL_LIBS dlfcn-win32::dl psapi.lib)
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        get_target_property(dlfcn-win32_LIBRARY dlfcn-win32::dl IMPORTED_LOCATION_DEBUG)
    else ()
        get_target_property(dlfcn-win32_LIBRARY dlfcn-win32::dl IMPORTED_LOCATION_RELEASE)
    endif ()
    message(STATUS "dlfcn-win32_LIBRARY: " ${dlfcn-win32_LIBRARY})
endif(MSVC)


set(LIB_NAME_SHARED ${LIB_NAME})

# this is the "object library" target: compiles the sources only once
add_library(PULSAR_OBJECT_LIB OBJECT ${PULSAR_SOURCES})
set_property(TARGET PULSAR_OBJECT_LIB PROPERTY POSITION_INDEPENDENT_CODE 1)
if (INTEGRATE_VCPKG)
    target_link_libraries(PULSAR_OBJECT_LIB PROTO_OBJECTS)
endif ()
target_include_directories(PULSAR_OBJECT_LIB PUBLIC
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_BINARY_DIR}/include")

option(LINK_STATIC_GCC_CXX "Link statically to libgcc and libstdc++" OFF)
include(CheckCXXSymbolExists)
if (BUILD_DYNAMIC_LIB)
    add_library(pulsarShared SHARED $<TARGET_OBJECTS:PULSAR_OBJECT_LIB>)
    set_property(TARGET pulsarShared PROPERTY OUTPUT_NAME ${LIB_NAME_SHARED})
    set_property(TARGET pulsarShared PROPERTY VERSION ${LIBRARY_VERSION})
    target_link_libraries(pulsarShared PRIVATE ${COMMON_LIBS} ${CMAKE_DL_LIBS})
    if (WIN32)
        # Add Windows system libraries required for CURL static linking
        target_link_libraries(pulsarShared PRIVATE ws2_32 wldap32 advapi32 crypt32)
    endif()
    target_include_directories(pulsarShared PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include)
    target_link_directories(pulsarShared PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
    if (MSVC)
        target_include_directories(pulsarShared PRIVATE ${dlfcn-win32_INCLUDE_DIRS})
        target_link_options(pulsarShared PRIVATE $<$<CONFIG:DEBUG>:/NODEFAULTLIB:MSVCRT>)
    endif()
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(pulsarShared PRIVATE -Wl,-Bsymbolic)
    endif ()
    check_cxx_symbol_exists(__GLIBCXX__ iostream GLIBCXX)
    if (GLIBCXX AND LINK_STATIC_GCC_CXX)
        target_link_libraries(pulsarShared PUBLIC -static-libgcc -static-libstdc++)
    endif ()
endif()

check_cxx_symbol_exists(getauxval sys/auxv.h HAVE_AUXV_GETAUXVAL)
if(HAVE_AUXV_GETAUXVAL)
    add_definitions(-DPULSAR_AUXV_GETAUXVAL_PRESENT)
endif()

if (BUILD_STATIC_LIB)
    add_library(pulsarStatic STATIC $<TARGET_OBJECTS:PULSAR_OBJECT_LIB>)
    target_include_directories(pulsarStatic PUBLIC
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include)
    if (MSVC)
        set_property(TARGET pulsarStatic PROPERTY OUTPUT_NAME "${LIB_NAME}-static")
        target_include_directories(pulsarStatic PRIVATE ${dlfcn-win32_INCLUDE_DIRS})
    else ()
        set_property(TARGET pulsarStatic PROPERTY OUTPUT_NAME ${LIB_NAME})
    endif()
    set_property(TARGET pulsarStatic PROPERTY VERSION ${LIBRARY_VERSION})
    target_compile_definitions(pulsarStatic PRIVATE PULSAR_STATIC)
endif()

# When linking statically, install a libpulsar.a that contains all the
# required dependencies except ssl
if (LINK_STATIC AND BUILD_STATIC_LIB)
    if (MSVC)
        set(COMMON_LIBS ${COMMON_LIBS} ${dlfcn-win32_LIBRARY})
        # This function is to remove either "debug" or "optimized" library names
        # out of the COMMON_LIBS list and return the sanitized list of libraries
        function(remove_libtype LIBLIST LIBTYPE OUTLIST)
            list(FIND LIBLIST ${LIBTYPE} LIST_INDEX)
            while(${LIST_INDEX} GREATER -1)
                list(REMOVE_AT LIBLIST ${LIST_INDEX})
                list(REMOVE_AT LIBLIST ${LIST_INDEX})
                list(FIND LIBLIST ${LIBTYPE} LIST_INDEX)
            endwhile()
            list(REMOVE_ITEM LIBLIST "debug")
            list(REMOVE_ITEM LIBLIST "optimized")
            foreach (ITEM IN LISTS LIBLIST)
                list(APPEND TEMP_OUT ${ITEM})
            endforeach ()
            set(${OUTLIST} ${TEMP_OUT} PARENT_SCOPE)
        endfunction(remove_libtype)

        add_library(pulsarStaticWithDeps STATIC ${PULSAR_SOURCES})
        target_include_directories(pulsarStaticWithDeps PRIVATE ${dlfcn-win32_INCLUDE_DIRS})
        if (CMAKE_BUILD_TYPE STREQUAL "Debug")
            remove_libtype("${COMMON_LIBS}" "optimized" STATIC_LIBS)
        else ()
            remove_libtype("${COMMON_LIBS}" "debug" STATIC_LIBS)
        endif ()
        set_property(TARGET pulsarStaticWithDeps PROPERTY STATIC_LIBRARY_OPTIONS ${STATIC_LIBS})
        set_property(TARGET pulsarStaticWithDeps PROPERTY OUTPUT_NAME ${LIB_NAME}WithDeps)
        set_property(TARGET pulsarStaticWithDeps PROPERTY VERSION ${LIBRARY_VERSION})
        install(TARGETS pulsarStaticWithDeps DESTINATION lib)
    else()
        # Build a list of the requird .a libs (except ssl) to merge
        SET(STATIC_LIBS "")
        foreach (LIB IN LISTS COMMON_LIBS)
            if (${LIB} MATCHES ".+\\.a$")
                set(STATIC_LIBS "${STATIC_LIBS} ${LIB}")
            endif()
        endforeach()

        set(PULSAR_WITH_DEPS ${CMAKE_BINARY_DIR}/lib/libpulsarwithdeps.a)
        add_custom_target(pulsarStaticWithDeps
                ALL
                BYPRODUCTS merged-library
                COMMAND ./build-support/merge_archives.sh libpulsar.a $<TARGET_FILE:pulsarStatic> ${STATIC_LIBS} && mv merged-library/libpulsar.a ${PULSAR_WITH_DEPS}
                DEPENDS pulsarStatic
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        install(PROGRAMS ${PULSAR_WITH_DEPS} DESTINATION lib)
    endif(MSVC)
elseif(BUILD_STATIC_LIB)
    # Install regular libpulsar.a
    target_link_libraries(pulsarStatic ${COMMON_LIBS})
    install(TARGETS pulsarStatic DESTINATION lib)
endif()

if (BUILD_STATIC_LIB)
    install(TARGETS pulsarStatic DESTINATION lib)
endif()

if (BUILD_DYNAMIC_LIB)
    install(TARGETS pulsarShared RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
endif()

install(DIRECTORY "../include/pulsar" DESTINATION include)
install(FILES "${PROJECT_BINARY_DIR}/include/pulsar/Version.h" DESTINATION include/pulsar/)
