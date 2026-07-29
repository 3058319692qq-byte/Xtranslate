// SharedOcrEngine - process-wide PaddleOcrEngine accessor.
//
// ONNX sessions are expensive to create, so all UI flows (截图OCR / 截图翻译)
// share one lazily-constructed engine; PaddleOcrEngine serializes session
// access internally.

#pragma once

class PaddleOcrEngine;

PaddleOcrEngine &sharedOcrEngine();
