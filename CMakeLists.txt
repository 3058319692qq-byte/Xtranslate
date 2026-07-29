cmake_minimum_required(VERSION 3.21)

project(XTranslate
    VERSION 0.7.2
    DESCRIPTION "XTranslate - screen translation tool (product name: X翻译)"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Language / global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# All runtime artifacts (exe + deployed dlls) land in build/bin.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ---------------------------------------------------------------------------
# Qt6  (CMAKE_PREFIX_PATH is injected by build.ps1 -> third_party/Qt/<ver>/msvc2022_64)
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS
    Core Gui Widgets Network Sql Svg Multimedia TextToSpeech Concurrent
    WebSockets)

# ---------------------------------------------------------------------------
# OpenCV  (static slim build: core + imgproc, installed under third_party/opencv-install)
# The Windows install places OpenCVConfig.cmake under x64/<vcXX>/staticlib.
# ---------------------------------------------------------------------------
if(NOT DEFINED OpenCV_DIR)
    file(GLOB _ocv_dirs
        "${CMAKE_SOURCE_DIR}/third_party/opencv-install/x64/*/staticlib"
        "${CMAKE_SOURCE_DIR}/third_party/opencv-install/x64/*/lib"
        "${CMAKE_SOURCE_DIR}/third_party/opencv-install/lib/cmake/opencv4")
    if(_ocv_dirs)
        list(GET _ocv_dirs 0 OpenCV_DIR)
    endif()
endif()
find_package(OpenCV REQUIRED COMPONENTS core imgproc)
message(STATUS "XTranslate: OpenCV ${OpenCV_VERSION} (OpenCV_DIR=${OpenCV_DIR})")

# ---------------------------------------------------------------------------
# ONNX Runtime  (prebuilt from NuGet -> imported SHARED target)
# ---------------------------------------------------------------------------
set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime")
if(NOT EXISTS "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
    message(FATAL_ERROR "onnxruntime.lib not found at ${ONNXRUNTIME_ROOT}/lib (run tools/fetch_onnx.py)")
endif()
add_library(onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
    IMPORTED_IMPLIB   "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_ROOT}/include")

# ---------------------------------------------------------------------------
# Reference models (bundled, Apache-2.0) copied next to the exe at build time.
# Phase 9: 模型改为工程内 models/ 目录（从 modelscope
# RapidAI/RapidOCR 仓库下载的官方 PP-OCRv6 small ONNX）。
# ---------------------------------------------------------------------------
set(XTRANSLATE_MODELS_SRC "${CMAKE_SOURCE_DIR}/models"
    CACHE PATH "Source tree of OCR models to deploy")

# ---------------------------------------------------------------------------
# Executable target (WIN32 subsystem since phase 5; --selftest reattaches the
# parent console via AttachConsole + freopen so JSON output & exit code stay
# intact when invoked from a terminal).
# ---------------------------------------------------------------------------
add_executable(XTranslate WIN32
    src/app/main.cpp
    src/app/DocShots.cpp
    src/app/SelfTest.cpp
    src/core/autostart/AutoStartManager.cpp
    src/core/capture/RegionSelector.cpp
    src/core/capture/ScreenCapturer.cpp
    src/core/config/ConfigManager.cpp
    src/core/hotkey/HotkeyManager.cpp
    src/core/ocr/OcrGrouping.cpp
    src/core/ocr/OcrEngineFactory.cpp
    src/core/ocr/PaddleOcrEngine.cpp
    src/core/ocr/SharedOcrEngine.cpp
    src/core/ocr/SystemOcrEngine.cpp
    src/core/plugin/PluginManager.cpp
    src/core/plugin/PluginTranslator.cpp
    src/core/replace/TextReplacer.cpp
    src/core/selection/SelectionGrabber.cpp
    src/core/storage/HistoryStore.cpp
    src/core/translate/GoogleFreeTranslator.cpp
    src/core/translate/BingFreeTranslator.cpp
    src/core/translate/MockTranslator.cpp
    src/core/translate/TranslationManager.cpp
    src/core/translate/providers/BaiduTranslator.cpp
    src/core/translate/providers/DeepLTranslator.cpp
    src/core/translate/providers/DeepLxTranslator.cpp
    src/core/translate/providers/LingvaTranslator.cpp
    src/core/translate/providers/OpenAiCompatTranslator.cpp
    src/core/translate/providers/TencentTranslator.cpp
    src/core/translate/providers/YoudaoTranslator.cpp
    src/core/tts/EdgeTtsProvider.cpp
    src/core/tts/TtsManager.cpp
    src/ui/MainWindow.cpp
    src/ui/OcrResultDialog.cpp
    src/ui/history/HistorySidebar.cpp
    src/ui/overlay/ControlBar.cpp
    src/ui/overlay/OverlayWindow.cpp
    src/ui/overlay/ScreenTranslateController.cpp
    src/ui/popup/PopupCard.cpp
    src/ui/settings/SettingsWindow.cpp
    src/ui/theme/ThemeManager.cpp
    src/ui/tray/TrayManager.cpp
    src/ui/platform/WinBackdrop.cpp
    resources/xtranslate.qrc
    resources/icons/icons.qrc
    resources/VERSIONINFO.rc)

# WIN32 subsystem: no console by default. --selftest attaches the parent
# process console on demand (see main.cpp); GUI runs stay black-window-free.
set_target_properties(XTranslate PROPERTIES WIN32_EXECUTABLE TRUE)

target_include_directories(XTranslate PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    ${OpenCV_INCLUDE_DIRS})

target_link_libraries(XTranslate PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Sql
    Qt6::Svg Qt6::Multimedia Qt6::TextToSpeech Qt6::Concurrent
    Qt6::WebSockets
    ${OpenCV_LIBS}
    onnxruntime)

if(MSVC)
    # /utf-8 so the embedded "X翻译" literals are interpreted as UTF-8.
    target_compile_options(XTranslate PRIVATE /utf-8)
endif()
if(WIN32)
    # user32: SetProcessDpiAwarenessContext / window ex-styles / RegisterHotKey
    #         / SendInput (phase 3) / SetWindowCompositionAttribute (phase 7);
    # gdi32 : BitBlt screen capture (phase 2);
    # ole32/oleaut32: COM + BSTR for the UI Automation selection fallback.
    # dwmapi: DwmSetWindowAttribute / DwmExtendFrameIntoClientArea /
    #         DwmGetWindowAttribute (phase 7 Liquid Glass backdrop).
    target_link_libraries(XTranslate PRIVATE user32 gdi32 ole32 oleaut32 dwmapi)

    # Phase 5: 探测 C++/WinRT 头（在 Windows SDK 的 cppwinrt 子目录下）。
    # 探测成功则启用 SystemOcrEngine（Windows.Media.Ocr），否则跳过，
    # --selftest systemocr 会如实报告不可用（available=false 也算 pass）。
    # C++/WinRT 是头-only 投影，不需要 /ZW，只需把 cppwinrt 目录加进 include
    # 路径并链接 windowsapp.lib（运行时 RoInitialize 等 API 的导入库）。
    find_path(XT_WINRT_INCLUDE_DIR
        NAMES winrt/Windows.Foundation.h
        PATHS "$ENV{WindowsSdkDir}Include/$ENV{WindowsSDKVersion}cppwinrt"
              "C:/Program Files (x86)/Windows Kits/10/Include"
        PATH_SUFFIXES cppwinrt
        NO_DEFAULT_PATH)
    if(NOT XT_WINRT_INCLUDE_DIR)
        # 兜底：在所有已安装的 SDK 版本里找最新的一份。
        file(GLOB _winrt_dirs "C:/Program Files (x86)/Windows Kits/10/Include/*/cppwinrt")
        if(_winrt_dirs)
            list(SORT _winrt_dirs COMPARE NATURAL ORDER DESCENDING)
            list(GET _winrt_dirs 0 XT_WINRT_INCLUDE_DIR)
        endif()
    endif()
    if(XT_WINRT_INCLUDE_DIR)
        message(STATUS "XTranslate: C++/WinRT headers at ${XT_WINRT_INCLUDE_DIR}")
        target_include_directories(XTranslate PRIVATE "${XT_WINRT_INCLUDE_DIR}")
        target_compile_definitions(XTranslate PRIVATE XT_HAVE_WINRT)
        target_link_libraries(XTranslate PRIVATE windowsapp)
    else()
        message(STATUS "XTranslate: C++/WinRT headers NOT found (systemocr selftest will report unavailable)")
    endif()
endif()

# Phase 9: 模型存在性检查——models/ 已被 .gitignore 排除，开发者克隆后
# 必须先跑 tools/fetch_models.ps1 下载官方 PP-OCRv6 small 模型。缺失时给出
# 清晰指引，而非让 copy_directory 抛晦涩错误。
set(_xt_model_dir "${XTRANSLATE_MODELS_SRC}/paddleocr/pp-ocrv6-small")
set(_xt_model_files
    "PP-OCRv6_small_det.onnx"
    "PP-OCRv6_small_rec.onnx"
    "ppocrv6_dict.txt")
foreach(_mf ${_xt_model_files})
    if(NOT EXISTS "${_xt_model_dir}/${_mf}")
        message(FATAL_ERROR
            "OCR model missing: ${_xt_model_dir}/${_mf}\n"
            "Run tools/fetch_models.ps1 first to download official PP-OCRv6 small "
            "models from PaddleOCR (Apache-2.0). See README.md 'Build from source'.")
    endif()
endforeach()

# Copy the OCR model tree -> build/bin/models after every build.
add_custom_command(TARGET XTranslate POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${XTRANSLATE_MODELS_SRC}"
        "$<TARGET_FILE_DIR:XTranslate>/models"
    COMMENT "Deploying OCR models -> $<TARGET_FILE_DIR:XTranslate>/models")
