cmake_minimum_required(VERSION 3.28)

project(AudioFormatConverter VERSION 1.0 LANGUAGES C CXX)

include(CTest)
include(GNUInstallDirs)
include(FetchContent)
cmake_policy(SET CMP0077 NEW)

foreach(_dependency_file IN ITEMS
        "${CMAKE_CURRENT_SOURCE_DIR}/external/CLI11/include/CLI/CLI.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/spdlog/include/spdlog/spdlog.h")
    if(NOT EXISTS "${_dependency_file}")
        message(FATAL_ERROR
            "Required Git submodule content is missing: '${_dependency_file}'. "
            "Run 'git submodule update --init --recursive'.")
    endif()
endforeach()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/sqlite-amalgamation/sqlite3.c")
    message(FATAL_ERROR "SQLite amalgamation missing under external/sqlite-amalgamation.")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/FlacConvert/include/rapidjson/document.h")
    message(FATAL_ERROR "Vendored RapidJSON headers missing under FlacConvert/include/rapidjson.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Load compiler checks before dependencies can capture legacy try_compile policies.
include(CheckCXXSourceCompiles)
check_cxx_source_compiles(
    "#include <format>
     int main() { return std::format(\"{}\", 1) == \"1\" ? 0 : 1; }"
    AFC_HAS_STD_FORMAT)
if(NOT AFC_HAS_STD_FORMAT)
    message(FATAL_ERROR "The selected C++ standard library must provide C++20 std::format.")
endif()

set(_afc_build_testing "${BUILD_TESTING}")
set(BUILD_TESTING OFF)
set(BUILD_SHARED_LIBS OFF)
set(BUILD_BINDINGS OFF)
set(BUILD_EXAMPLES OFF)
FetchContent_Declare(taglib
    GIT_REPOSITORY https://github.com/taglib/taglib.git
    GIT_TAG 7d86716194777e0294453bfdc9dd170bd033e1f4
    GIT_SUBMODULES_RECURSE TRUE)
FetchContent_MakeAvailable(taglib)
set(BUILD_TESTING "${_afc_build_testing}")
add_library(TagLib::TagLib ALIAS tag)
target_include_directories(tag INTERFACE
    "$<BUILD_INTERFACE:${taglib_SOURCE_DIR}>"
    "$<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib>"
    "$<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib/toolkit>"
    "$<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib/mpeg>"
    "$<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib/mpeg/id3v2>"
    "$<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib/mpeg/id3v2/frames>"
    "$<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib/mpeg/id3v1>"
    "$<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib/ogg>"
    "$<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib/ogg/flac>"
    "$<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib/ogg/vorbis>"
    "$<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib/ogg/speex>"
    "$<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib/ogg/opus>"
    "$<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib/flac>"
    "$<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib/ape>"
    "$<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib/mpc>"
    "$<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib/wavpack>"
    "$<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib/mp4>"
    "$<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib/riff>"
    "$<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib/riff/aiff>"
    "$<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib/riff/wav>"
    "$<BUILD_INTERFACE:${taglib_BINARY_DIR}>")

find_package(Threads REQUIRED)

if(WIN32)
    include(cmake/platform/Windows.cmake)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(cmake/platform/Linux.cmake)
else()
    message(FATAL_ERROR "Unsupported target platform: ${CMAKE_SYSTEM_NAME}")
endif()

add_subdirectory(FlacConvert)

install(TARGETS FlacConvert
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(WIN32)
    install(FILES ${_ffmpeg_runtime_dlls}
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
