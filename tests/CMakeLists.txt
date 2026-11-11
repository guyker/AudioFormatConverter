find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(preservation_audit_test preservation_audit.cpp)
target_link_libraries(preservation_audit_test PRIVATE FlacConvertCore)
add_executable(warning_regressions_test warning_regressions.cpp)
target_link_libraries(warning_regressions_test PRIVATE FlacConvertCore)
add_executable(media_metadata_json_test media_metadata_json.cpp)
target_link_libraries(media_metadata_json_test PRIVATE FlacConvertCore)
add_executable(bounded_task_runner_test bounded_task_runner.cpp)
target_include_directories(bounded_task_runner_test PRIVATE
    "${PROJECT_SOURCE_DIR}/FlacConvert/include")
target_compile_features(bounded_task_runner_test PRIVATE cxx_std_20)

foreach(_target IN ITEMS preservation_audit_test warning_regressions_test media_metadata_json_test bounded_task_runner_test)
    target_compile_options(${_target} PRIVATE
        "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/W3;/utf-8;/permissive->"
        "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>>:-Wall;-Wextra;-Wpedantic>")
    if(WIN32)
        afc_copy_windows_ffmpeg_runtime(${_target})
    endif()
endforeach()

add_test(NAME preservation-audit
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_native_regression.py"
        preservation
        "$<TARGET_FILE:preservation_audit_test>"
        "${FFMPEG_EXECUTABLE}")
add_test(NAME warning-regressions
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_native_regression.py"
        warnings
        "$<TARGET_FILE:warning_regressions_test>"
        "${FFMPEG_EXECUTABLE}")
add_test(NAME media-metadata-json
    COMMAND "$<TARGET_FILE:media_metadata_json_test>")
add_test(NAME bounded-task-runner
    COMMAND "$<TARGET_FILE:bounded_task_runner_test>")
add_test(NAME mp3-conversion-integration
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/mp3_conversion.py"
        --converter "$<TARGET_FILE:FlacConvert>"
        --ffmpeg "${FFMPEG_EXECUTABLE}"
        --ffprobe "${FFPROBE_EXECUTABLE}"
        --audit-checker "$<TARGET_FILE:preservation_audit_test>")
set_tests_properties(mp3-conversion-integration PROPERTIES TIMEOUT 900)
