add_library(sqlite3 STATIC
    "${PROJECT_SOURCE_DIR}/external/sqlite-amalgamation/sqlite3.c")
add_library(SQLite::SQLite3 ALIAS sqlite3)
target_include_directories(sqlite3 PUBLIC
    "${PROJECT_SOURCE_DIR}/external/sqlite-amalgamation")
target_link_libraries(sqlite3 PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
set_target_properties(sqlite3 PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)

add_library(CLI11 INTERFACE)
target_include_directories(CLI11 SYSTEM INTERFACE
    "${PROJECT_SOURCE_DIR}/external/CLI11/include")

add_library(spdlog_headers INTERFACE)
target_include_directories(spdlog_headers SYSTEM INTERFACE
    "${PROJECT_SOURCE_DIR}/external/spdlog/include")

add_library(rapidjson_headers INTERFACE)
target_include_directories(rapidjson_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(FlacConvertCore STATIC
    src/AlbumCollection.cpp
    src/AppSettings.cpp
    src/CommonUtils.cpp
    src/CommandLine.cpp
    src/ffmpeg.cpp
    src/FolderConvert.cpp
    src/FlacMetadata.cpp
    src/PreservationAudit.cpp
    src/JsonUtils.cpp
    src/MediaAlbum.cpp
    src/MediaConvertionAsyncTask.cpp
    src/MediaConvertionTask.cpp
    src/MediaTrack.cpp
    src/PlatformUtils.cpp)

target_include_directories(FlacConvertCore PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(FlacConvertCore
    PUBLIC
        CLI11
        FFmpeg::FFmpeg
        SQLite::SQLite3
        spdlog_headers
        Threads::Threads
    PRIVATE
        rapidjson_headers)

add_executable(FlacConvert src/FlacConvertMain.cpp)
target_link_libraries(FlacConvert PRIVATE FlacConvertCore)

foreach(_target IN ITEMS sqlite3 FlacConvertCore FlacConvert)
    set_target_properties(${_target} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    target_compile_options(${_target} PRIVATE
        "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/W3;/utf-8;/permissive->"
        "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>>:-Wall;-Wextra;-Wpedantic>")
endforeach()

if(WIN32)
    target_compile_definitions(FlacConvertCore PUBLIC UNICODE _UNICODE)
    target_link_libraries(FlacConvertCore PUBLIC shell32)
    afc_copy_windows_ffmpeg_runtime(FlacConvert)
    afc_embed_windows_manifest(FlacConvert)
endif()
