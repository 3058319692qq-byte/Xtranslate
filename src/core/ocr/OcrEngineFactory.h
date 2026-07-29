// OcrEngineFactory - 按 config 选择 OCR 引擎（phase 5）。
//
// 配置项 ocr.engine ∈ {"paddle", "system"}，默认 "paddle"。
//   paddle : PaddleOcrEngine（ONNX 模型，单例）
//   system : SystemOcrEngine（Windows.Media.Ocr，WinRT 不可用时降级回 paddle）
//
// 调用点（MainWindow::runOcrOnRegion / ScreenTranslateController / selftest）
// 改用 factory.engine() 取引擎，不再直接 sharedOcrEngine()。selftest 的
// paddle 专用用例（runOcr/runCapture）仍直接走 sharedOcrEngine() 以保持契约。

#pragma once

#include "core/ocr/OcrEngine.h"

class PaddleOcrEngine;
class SystemOcrEngine;

class OcrEngineFactory
{
public:
    static OcrEngineFactory &instance();

    // 返回当前 config 选中的引擎；system 不可用时降级回 paddle。
    // 返回值始终非空（paddle 总是可构造，model 缺失在 recognize 时报错）。
    OcrEngine *engine();

    // paddle 单例（selftest 与 SharedOcrEngine 兼容入口）。
    PaddleOcrEngine &paddle();

    // system 单例（仅用于 selftest 探测，不参与生产路径）。
    SystemOcrEngine &system();

    // 当前选中的引擎名（"paddle" / "system"），含降级后的实际值。
    QString currentName() const { return m_lastName; }

private:
    OcrEngineFactory();

    PaddleOcrEngine *m_paddle = nullptr;
    SystemOcrEngine *m_system = nullptr;
    QString m_lastName;
};
