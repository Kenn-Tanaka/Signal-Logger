cmake_minimum_required(VERSION 3.26)
project(SignalLogger LANGUAGES C CXX)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ========================================================
# 依存ライブラリの解決 (.deps/install 優先、3rdparty フォールバック)
# ========================================================
# 依存ディレクトリの指定 (明示指定 -DDLOG_DEPS_DIR または既定 .deps/install)
if (NOT DLOG_DEPS_DIR)
    set(DLOG_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps/install")
endif()

# 検証済み依存セットの判定 (.deps/install/build_completed.json および全必須ライブラリ・setup.h)
if (EXISTS "${DLOG_DEPS_DIR}/build_completed.json" AND
    EXISTS "${DLOG_DEPS_DIR}/lib/libportaudio.a" AND
    EXISTS "${DLOG_DEPS_DIR}/lib/gcc_x64_lib/libwxmsw32u_core.a" AND
    EXISTS "${DLOG_DEPS_DIR}/lib/gcc_x64_lib/libwxbase32u.a" AND
    EXISTS "${DLOG_DEPS_DIR}/lib/gcc_x64_lib/mswu/wx/setup.h" AND
    EXISTS "${DLOG_DEPS_DIR}/include/portaudio.h" AND
    EXISTS "${DLOG_DEPS_DIR}/include/readerwriterqueue.h")
    set(DEPS_INSTALL_DIR "${DLOG_DEPS_DIR}")
    message(STATUS "Using verified official dependencies from ${DEPS_INSTALL_DIR}")
    set(PA_LIB_DIR "${DEPS_INSTALL_DIR}/lib")
    set(PA_INC_DIR "${DEPS_INSTALL_DIR}/include")
    set(WX_LIB_DIR "${DEPS_INSTALL_DIR}/lib/gcc_x64_lib")
    set(WX_INC_DIR "${DEPS_INSTALL_DIR}/include")
    set(WX_SETUP_DIR "${DEPS_INSTALL_DIR}/lib/gcc_x64_lib/mswu")
    set(QUEUE_INC_DIR "${DEPS_INSTALL_DIR}/include")
else()
    message(FATAL_ERROR "Complete verified dependencies not found at '${DLOG_DEPS_DIR}'. Run scripts/build-deps.ps1 to build dependencies from official sources.")
endif()

# ========================================================
# Imported Targets 定義
# ========================================================
# PortAudio 静的ライブラリ
add_library(PortAudio::PortAudio STATIC IMPORTED GLOBAL)
set_target_properties(PortAudio::PortAudio PROPERTIES
    IMPORTED_LOCATION "${PA_LIB_DIR}/libportaudio.a"
    INTERFACE_INCLUDE_DIRECTORIES "${PA_INC_DIR}"
)

# Windows システムライブラリ群
set(SYSTEM_LIBS
    winmm
    dsound
    ole32
    oleaut32
    oleacc
    uuid
    setupapi
    comctl32
    comdlg32
    uxtheme
    gdi32
    user32
    shell32
    advapi32
    version
    shlwapi
    ws2_32
)

# wxWidgets 静的ライブラリ群
set(WX_STATIC_LIBS
    "${WX_LIB_DIR}/libwxmsw32u_core.a"
    "${WX_LIB_DIR}/libwxbase32u.a"
    "${WX_LIB_DIR}/libwxpng.a"
    "${WX_LIB_DIR}/libwxjpeg.a"
    "${WX_LIB_DIR}/libwxtiff.a"
    "${WX_LIB_DIR}/libwxzlib.a"
    "${WX_LIB_DIR}/libwxregexu.a"
)

add_library(wxWidgets::wxWidgets INTERFACE IMPORTED GLOBAL)
set_target_properties(wxWidgets::wxWidgets PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${WX_INC_DIR};${WX_SETUP_DIR}"
    INTERFACE_LINK_LIBRARIES "${WX_STATIC_LIBS};${SYSTEM_LIBS}"
)

# 共通インクルードディレクトリ
set(COMMON_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src_converter
    ${QUEUE_INC_DIR}
    ${PA_INC_DIR}
    ${WX_INC_DIR}
    ${WX_SETUP_DIR}
)

# 共通コンパイル定義
set(COMMON_DEFINITIONS
    UNICODE
    _UNICODE
    __WXMSW__
    wxUSE_GUI=1
    wxUSE_BASE=1
    _WIN32_WINNT=0x0601
    WINVER=0x0601
)

# 共通リンクオプション (完全静的リンク)
set(COMMON_LINK_OPTIONS
    -static
    -static-libgcc
    -static-libstdc++
    -Wl,--allow-multiple-definition
    -mwindows
)

# ========================================================
# 共通ローカライズ・言語設定ソース
set(COMMON_SOURCES
    src/common/ui_language.h
    src/common/ui_language.cpp
    src/common/language_preferences.h
    src/common/language_preferences.cpp
    src/common/localized_strings.h
    src/common/localized_strings.cpp
)

set(EXPORT_SOURCES
    src/export/wav_export_types.h
    src/export/wav_export_plan.h
    src/export/wav_export_plan.cpp
    src/export/interpolator.h
    src/export/interpolator.cpp
    src/export/wav_writer.h
    src/export/wav_writer.cpp
    src/export/wav_export_worker.h
    src/export/wav_export_worker.cpp
)

# ========================================================
# 1. データロガー本体 (SignalLogger.exe)
# ========================================================
set(LOGGER_SOURCES
    ${COMMON_SOURCES}
    ${EXPORT_SOURCES}
    src/main.cpp
    src/app.h
    src/app.cpp
    src/audio/audio_types.h
    src/audio/audio_device_mgr.h
    src/audio/audio_device_mgr.cpp
    src/dsp/decimator_processor.h
    src/dsp/decimator_processor.cpp
    src/dsp/calibration_calculator.h
    src/dsp/calibration_calculator.cpp
    src/storage/power_manager.h
    src/storage/signal_logger_worker.h
    src/storage/signal_logger_worker.cpp
    src/ui/waveform_panel.h
    src/ui/waveform_panel.cpp
    src/ui/time_axis_calculator.h
    src/ui/time_axis_calculator.cpp
    src/playback/playback_types.h
    src/playback/playback_time_parser.h
    src/playback/playback_time_parser.cpp
    src/playback/playback_loader_worker.h
    src/playback/playback_loader_worker.cpp
    src_converter/dlog_reader.h
    src_converter/dlog_reader.cpp
    src/ui/calibration_dialog.h
    src/ui/calibration_dialog.cpp
    src/ui/wav_export_dialog.h
    src/ui/wav_export_dialog.cpp
    src/ui/main_frame.h
    src/ui/main_frame.cpp
)

add_executable(SignalLogger ${LOGGER_SOURCES})
target_include_directories(SignalLogger PRIVATE ${COMMON_INCLUDES})
target_compile_definitions(SignalLogger PRIVATE ${COMMON_DEFINITIONS})
target_link_libraries(SignalLogger PRIVATE PortAudio::PortAudio wxWidgets::wxWidgets ${SYSTEM_LIBS})
target_link_options(SignalLogger PRIVATE ${COMMON_LINK_OPTIONS})

# ========================================================
# 2. DLOG to CSV バッチ変換ツール (SlogToCsv.exe)
# ========================================================
set(CONVERTER_SOURCES
    ${COMMON_SOURCES}
    src_converter/main_converter.cpp
    src_converter/converter_app.h
    src_converter/converter_app.cpp
    src_converter/converter_types.h
    src_converter/dlog_reader.h
    src_converter/dlog_reader.cpp
    src_converter/csv_writer.h
    src_converter/csv_writer.cpp
    src_converter/batch_converter_worker.h
    src_converter/batch_converter_worker.cpp
    src_converter/ui/converter_frame.h
    src_converter/ui/converter_frame.cpp
)

add_executable(SlogToCsv ${CONVERTER_SOURCES})
target_include_directories(SlogToCsv PRIVATE ${COMMON_INCLUDES})
target_compile_definitions(SlogToCsv PRIVATE ${COMMON_DEFINITIONS})
target_link_libraries(SlogToCsv PRIVATE wxWidgets::wxWidgets ${SYSTEM_LIBS})
target_link_options(SlogToCsv PRIVATE ${COMMON_LINK_OPTIONS})

if(WIN32)
    set(APP_ICON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/resources")
    target_sources(SignalLogger PRIVATE "${APP_ICON_DIR}/signal_logger.rc")
    target_sources(SlogToCsv PRIVATE "${APP_ICON_DIR}/slog_to_csv.rc")
    target_include_directories(SignalLogger PRIVATE "${APP_ICON_DIR}")
    target_include_directories(SlogToCsv PRIVATE "${APP_ICON_DIR}")
    set_property(SOURCE "${APP_ICON_DIR}/signal_logger.rc" APPEND PROPERTY
        OBJECT_DEPENDS "${APP_ICON_DIR}/signal_logger.ico")
    set_property(SOURCE "${APP_ICON_DIR}/slog_to_csv.rc" APPEND PROPERTY
        OBJECT_DEPENDS "${APP_ICON_DIR}/slog_to_csv.ico")
endif()

# ========================================================
# 3. テストターゲット (CTest 統合)
# ========================================================
enable_testing()

# 3.1 データロガーコアテスト (test_dlog)
set(TEST_DLOG_SOURCES
    ${COMMON_SOURCES}
    tests/test_dlog.cpp
    src/audio/audio_device_mgr.cpp
    src/dsp/decimator_processor.cpp
    src/dsp/calibration_calculator.cpp
    src/storage/signal_logger_worker.cpp
    src/ui/time_axis_calculator.cpp
    src/playback/playback_time_parser.cpp
    src/playback/playback_loader_worker.cpp
    src_converter/dlog_reader.cpp
)

add_executable(test_dlog ${TEST_DLOG_SOURCES})
target_include_directories(test_dlog PRIVATE ${COMMON_INCLUDES})
target_compile_definitions(test_dlog PRIVATE
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0601
    WINVER=0x0601
)
target_link_libraries(test_dlog PRIVATE
    PortAudio::PortAudio
    winmm
    ole32
    uuid
    setupapi
)
target_link_options(test_dlog PRIVATE
    -static
    -static-libgcc
    -static-libstdc++
)

add_test(NAME TestSignalLogger COMMAND $<TARGET_FILE:test_dlog>)
option(DLOG_ENABLE_HARDWARE_TESTS "Enable tests that capture live audio (opt-in)" OFF)
if(DLOG_ENABLE_HARDWARE_TESTS)
    add_test(NAME TestAudioHardware COMMAND $<TARGET_FILE:test_dlog> --hardware-only)
    set_tests_properties(TestAudioHardware PROPERTIES LABELS "hardware" TIMEOUT 30)
endif()

# 3.2 コンバータコアテスト (test_converter)
set(TEST_CONVERTER_SOURCES
    ${COMMON_SOURCES}
    tests/test_converter.cpp
    src_converter/dlog_reader.cpp
    src_converter/csv_writer.cpp
    src_converter/batch_converter_worker.cpp
)

add_executable(test_converter ${TEST_CONVERTER_SOURCES})
target_include_directories(test_converter PRIVATE ${COMMON_INCLUDES})
target_compile_definitions(test_converter PRIVATE
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0601
    WINVER=0x0601
)
target_link_options(test_converter PRIVATE
    -static
    -static-libgcc
    -static-libstdc++
)

add_test(NAME TestConverter COMMAND $<TARGET_FILE:test_converter>)

# 3.3 WAVエクスポートコアテスト (test_wav_export)
set(TEST_WAV_EXPORT_SOURCES
    ${COMMON_SOURCES}
    tests/test_wav_export.cpp
    src/playback/playback_time_parser.cpp
    src_converter/dlog_reader.cpp
    src/export/wav_export_plan.cpp
    src/export/interpolator.cpp
    src/export/wav_writer.cpp
    src/export/wav_export_worker.cpp
)

add_executable(test_wav_export ${TEST_WAV_EXPORT_SOURCES})
target_include_directories(test_wav_export PRIVATE ${COMMON_INCLUDES})
target_compile_definitions(test_wav_export PRIVATE
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0601
    WINVER=0x0601
)
target_link_options(test_wav_export PRIVATE
    -static
    -static-libgcc
    -static-libstdc++
)

add_test(NAME TestWavExport COMMAND $<TARGET_FILE:test_wav_export>)

# 3.3 GUI 起動・初期化スモークテスト (SignalLogger / SlogToCsv × 日本語 / 英語)
add_test(NAME TestSignalLoggerSmokeJa COMMAND $<TARGET_FILE:SignalLogger> --smoke-test --language=ja)
set_tests_properties(TestSignalLoggerSmokeJa PROPERTIES TIMEOUT 15)

add_test(NAME TestSignalLoggerSmokeEn COMMAND $<TARGET_FILE:SignalLogger> --smoke-test --language=en)
set_tests_properties(TestSignalLoggerSmokeEn PROPERTIES TIMEOUT 15)

add_test(NAME TestSlogToCsvSmokeJa COMMAND $<TARGET_FILE:SlogToCsv> --smoke-test --language=ja)
set_tests_properties(TestSlogToCsvSmokeJa PROPERTIES TIMEOUT 15)

add_test(NAME TestSlogToCsvSmokeEn COMMAND $<TARGET_FILE:SlogToCsv> --smoke-test --language=en)
set_tests_properties(TestSlogToCsvSmokeEn PROPERTIES TIMEOUT 15)
