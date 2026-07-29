// SystemOcrEngine - Windows.Media.Ocr 包装（phase 5）。
//
// 通过 C++/WinRT 头投影调用系统 OCR（WinRT OcrEngine），不依赖 ONNX 模型，
// 适合 paddle 模型缺失或用户偏好系统引擎的场景。识别结果按行返回，每行 quad
// 退化为整图外接矩形（WinRT OcrLine 不暴露像素坐标，仅给文字 + 旋转角度）。
//
// 仅在 XT_HAVE_WINRT 定义时可用；否则 isAvailable()=false，recognize() 直接
// 返回 error="winrt unavailable"。

#pragma once

#include "core/ocr/OcrEngine.h"

class SystemOcrEngine : public OcrEngine
{
public:
    SystemOcrEngine();
    ~SystemOcrEngine() override;

    QFuture<OcrResult> recognize(const QImage &image) override;

    // 是否可用：WinRT 头存在 + RoInitialize 成功 + 至少一个 OcrEngine 可建。
    static bool isAvailable();

    // 用户语言标签（如 "zh-CN"、"en"）→ 系统支持的语言 tag；
    // 找不到精确匹配时返回空（调用方降级到 paddle 或报错）。
    static QString pickLanguageTag(const QString &hint);

private:
    OcrResult recognizeSync(const QImage &image);
};
