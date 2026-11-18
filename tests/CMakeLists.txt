find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(preservation_audit_test preservation_audit.cpp)
target_link_libraries(preservation_audit_test PRIVATE FlacConvertCore)
add_executable(warning_regressions_test warning_regressions.cpp)
target_link_libraries(warning_regressions_test PRIVATE FlacConvertCore)
add_executable(media_metadata_json_test media_metadata_json.cpp)
target_link_libraries(media_metadata_json_test PRIVATE FlacConvertCore)
add_executable(media_record_json_test media_record_json.cpp)
target_link_libraries(media_record_json_test PRIVATE FlacConvertCore)
add_executable(taglib_smoke_test taglib_smoke.cpp)
target_link_libraries(taglib_smoke_test PRIVATE FlacConvertCore)
add_executable(decode_analyzer_test decode_analyzer.cpp)
target_link_libraries(decode_analyzer_test PRIVATE FlacConvertCore)
add_executable(bounded_task_runner_test bounded_task_runner.cpp)
target_include_directories(bounded_task_runner_test PRIVATE
    "${PROJECT_SOURCE_DIR}/FlacConvert/include")
target_compile_features(bounded_task_runner_test PRIVATE cxx_std_20)
add_executable(scoped_current_directory_test scoped_current_directory.cpp)
target_link_libraries(scoped_current_directory_test PRIVATE FlacConvertCore)
add_executable(scan_report_schema_test scan_report_schema.cpp)
target_link_libraries(scan_report_schema_test PRIVATE FlacConvertCore)

foreach(_target IN ITEMS preservation_audit_test warning_regressions_test media_metadata_json_test media_record_json_test taglib_smoke_test decode_analyzer_test bounded_task_runner_test scoped_current_directory_test scan_report_schema_test)
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
add_test(NAME media-record-json
    COMMAND "$<TARGET_FILE:media_record_json_test>"
        "${PROJECT_SOURCE_DIR}/docs/schemas/media-catalog-v2.schema.json")
add_test(NAME scan-root-level-audio
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scan_root_audio.py"
        "$<TARGET_FILE:FlacConvert>"
        "${FFMPEG_EXECUTABLE}"
        "$<TARGET_FILE:scan_report_schema_test>"
        "${PROJECT_SOURCE_DIR}/docs/schemas/scan-report-v2.schema.json"
        "${PROJECT_SOURCE_DIR}/docs/schemas/scan-manifest-v1.schema.json")
add_test(NAME taglib-reader-smoke
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_native_regression.py"
        taglib
        "$<TARGET_FILE:taglib_smoke_test>"
        "${FFMPEG_EXECUTABLE}")
add_test(NAME decode-analyzer
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/decode_analyzer.py"
        --analyzer "$<TARGET_FILE:decode_analyzer_test>"
        --ffmpeg "${FFMPEG_EXECUTABLE}")
add_test(NAME bounded-task-runner
    COMMAND "$<TARGET_FILE:bounded_task_runner_test>")
add_test(NAME scoped-current-directory
    COMMAND "$<TARGET_FILE:scoped_current_directory_test>")
add_test(NAME mp3-conversion-integration
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/mp3_conversion.py"
        --converter "$<TARGET_FILE:FlacConvert>"
        --ffmpeg "${FFMPEG_EXECUTABLE}"
        --ffprobe "${FFPROBE_EXECUTABLE}"
        --audit-checker "$<TARGET_FILE:preservation_audit_test>")
set_tests_properties(mp3-conversion-integration PROPERTIES TIMEOUT 900)
