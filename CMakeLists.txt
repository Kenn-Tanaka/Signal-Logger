cmake_minimum_required(VERSION 3.26)
project(DataLoggerSuite LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ========================================================
# 依存ライブラリの解決 (.deps/install 優先、3rdparty フォールバック)
# ========================================================
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/install/lib/libportaudio.a")
    set(DEPS_INSTALL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps/install")
    message(STATUS "Using dependencies from .deps/install")
    set(PA_LIB_DIR "${DEPS_INSTALL_DIR}/lib")
    set(PA_INC_DIR "${DEPS_INSTALL_DIR}/include")
    set(WX_LIB_DIR "${DEPS_INSTALL_DIR}/lib/gcc_x64_lib")
    set(WX_INC_DIR "${DEPS_INSTALL_DIR}/include")
    set(WX_SETUP_DIR "${DEPS_INSTALL_DIR}/lib/gcc_x64_lib/mswu")
    set(QUEUE_INC_DIR "${DEPS_INSTALL_DIR}/include")
elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/portaudio/lib/libportaudio.a")
    message(STATUS "Using dependencies from 3rdparty/ (local fallback)")
    set(PA_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/portaudio/lib")
    set(PA_INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/portaudio/include")
    set(WX_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/wxWidgets/lib/gcc_x64_lib")
    set(WX_INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/wxWidgets/include")
    set(WX_SETUP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/wxWidgets/lib/gcc_x64_lib/mswu")
    set(QUEUE_INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/readerwriterqueue")
else()
    message(FATAL_ERROR "Dependencies not found. Run scripts/build-deps.ps1 to build dependencies from official sources.")
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
# 1. データロガー本体 (DataLogger.exe)
# ========================================================
set(LOGGER_SOURCES
    src/main.cpp
    src/app.h
    src/app.cpp
    src/audio/audio_types.h
    src/audio/audio_device_mgr.h
    src/audio/audio_device_mgr.cpp
    src/dsp/decimator_processor.h
    src/dsp/decimator_processor.cpp
    src/storage/power_manager.h
    src/storage/data_logger_worker.h
    src/storage/data_logger_worker.cpp
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
    src/ui/main_frame.h
    src/ui/main_frame.cpp
)

add_executable(DataLogger ${LOGGER_SOURCES})
target_include_directories(DataLogger PRIVATE ${COMMON_INCLUDES})
target_compile_definitions(DataLogger PRIVATE ${COMMON_DEFINITIONS})
target_link_libraries(DataLogger PRIVATE PortAudio::PortAudio wxWidgets::wxWidgets ${SYSTEM_LIBS})
target_link_options(DataLogger PRIVATE ${COMMON_LINK_OPTIONS})

# ========================================================
# 2. DLOG to CSV バッチ変換ツール (DlogToCsv.exe)
# ========================================================
set(CONVERTER_SOURCES
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

add_executable(DlogToCsv ${CONVERTER_SOURCES})
target_include_directories(DlogToCsv PRIVATE ${COMMON_INCLUDES})
target_compile_definitions(DlogToCsv PRIVATE ${COMMON_DEFINITIONS})
target_link_libraries(DlogToCsv PRIVATE wxWidgets::wxWidgets ${SYSTEM_LIBS})
target_link_options(DlogToCsv PRIVATE ${COMMON_LINK_OPTIONS})

# ========================================================
# 3. テストターゲット (CTest 統合)
# ========================================================
enable_testing()

# 3.1 データロガーコアテスト (test_dlog)
set(TEST_DLOG_SOURCES
    tests/test_dlog.cpp
    src/audio/audio_device_mgr.cpp
    src/dsp/decimator_processor.cpp
    src/storage/data_logger_worker.cpp
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

add_test(NAME TestDataLogger COMMAND test_dlog)

# 3.2 コンバータコアテスト (test_converter)
set(TEST_CONVERTER_SOURCES
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

add_test(NAME TestConverter COMMAND test_converter)

# 3.3 GUI 起動・初期化スモークテスト (DataLogger --smoke-test)
add_test(NAME TestSmokeTest COMMAND DataLogger --smoke-test)
set_tests_properties(TestSmokeTest PROPERTIES TIMEOUT 15)
