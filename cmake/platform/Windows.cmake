if(NOT MSVC)
    message(FATAL_ERROR "Windows builds currently require the MSVC C++ toolchain.")
endif()

if(NOT DEFINED FFmpeg_ROOT)
    if(DEFINED ENV{FFmpeg_ROOT})
        set(_ffmpeg_default_root "$ENV{FFmpeg_ROOT}")
    else()
        set(_ffmpeg_default_root
            "${PROJECT_SOURCE_DIR}/external/ffmpeg/ffmpeg-master-latest-win64-gpl-shared")
    endif()
    set(FFmpeg_ROOT "${_ffmpeg_default_root}" CACHE PATH
        "FFmpeg SDK root (Windows); Linux uses pkg-config")
endif()

set(_ffmpeg_include_dir "${FFmpeg_ROOT}/include")
set(_ffmpeg_library_dir "${FFmpeg_ROOT}/lib")
set(_ffmpeg_binary_dir "${FFmpeg_ROOT}/bin")

foreach(_required_path IN ITEMS "${_ffmpeg_include_dir}/libavformat/avformat.h" "${_ffmpeg_library_dir}")
    if(NOT EXISTS "${_required_path}")
        message(FATAL_ERROR
            "FFmpeg SDK not found at '${FFmpeg_ROOT}'. Set FFmpeg_ROOT to a matching shared FFmpeg SDK.")
    endif()
endforeach()

set(_ffmpeg_libraries)
foreach(_component IN ITEMS avformat avcodec avfilter avutil swresample)
    set(_ffmpeg_${_component}_library "${_ffmpeg_library_dir}/${_component}.lib")
    if(NOT EXISTS "${_ffmpeg_${_component}_library}")
        message(FATAL_ERROR "Missing ${_component} import library under '${_ffmpeg_library_dir}'.")
    endif()
    list(APPEND _ffmpeg_libraries "${_ffmpeg_${_component}_library}")
endforeach()

file(GLOB _ffmpeg_runtime_dlls CONFIGURE_DEPENDS "${_ffmpeg_binary_dir}/*.dll")
if(NOT _ffmpeg_runtime_dlls)
    message(FATAL_ERROR "No FFmpeg runtime DLLs found under '${_ffmpeg_binary_dir}'.")
endif()

add_library(FFmpeg::FFmpeg INTERFACE IMPORTED GLOBAL)
set_target_properties(FFmpeg::FFmpeg PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_ffmpeg_include_dir}"
    INTERFACE_LINK_LIBRARIES "${_ffmpeg_libraries}")

find_program(FFMPEG_EXECUTABLE NAMES ffmpeg.exe HINTS "${_ffmpeg_binary_dir}" REQUIRED)
find_program(FFPROBE_EXECUTABLE NAMES ffprobe.exe HINTS "${_ffmpeg_binary_dir}" REQUIRED)
find_program(MSVC_MT_EXECUTABLE NAMES mt.exe REQUIRED)

function(afc_copy_windows_ffmpeg_runtime target)
    add_custom_command(TARGET "${target}" POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            ${_ffmpeg_runtime_dlls} "$<TARGET_FILE_DIR:${target}>"
        COMMAND_EXPAND_LISTS
        VERBATIM)
endfunction()

function(afc_embed_windows_manifest target)
    add_custom_command(TARGET "${target}" POST_BUILD
        COMMAND "${MSVC_MT_EXECUTABLE}"
            -manifest "${PROJECT_SOURCE_DIR}/FlacConvert/app.manifest"
            "-updateresource:$<TARGET_FILE:${target}>"
        VERBATIM)
endfunction()
