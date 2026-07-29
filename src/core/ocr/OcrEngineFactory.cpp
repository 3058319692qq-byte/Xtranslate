#include "core/ocr/OcrEngineFactory.h"

#include "core/config/ConfigManager.h"
#include "core/ocr/PaddleOcrEngine.h"
#include "core/ocr/SystemOcrEngine.h"

OcrEngineFactory &OcrEngineFactory::instance()
{
    static OcrEngineFactory factory;
    return factory;
}

OcrEngineFactory::OcrEngineFactory() = default;

PaddleOcrEngine &OcrEngineFactory::paddle()
{
    if (!m_paddle)
        m_paddle = new PaddleOcrEngine();
    return *m_paddle;
}

SystemOcrEngine &OcrEngineFactory::system()
{
    if (!m_system)
        m_system = new SystemOcrEngine();
    return *m_system;
}

OcrEngine *OcrEngineFactory::engine()
{
    const QString name = ConfigManager::instance().ocrEngine().toLower();
    if (name == QLatin1String("system") && SystemOcrEngine::isAvailable()) {
        m_lastName = QStringLiteral("system");
        return &system();
    }
    // paddle 是默认 + 兜底（system 不可用时降级）。
    m_lastName = QStringLiteral("paddle");
    return &paddle();
}
