#include "core/ocr/SharedOcrEngine.h"

#include "core/ocr/PaddleOcrEngine.h"

PaddleOcrEngine &sharedOcrEngine()
{
    static PaddleOcrEngine engine;
    return engine;
}
