#include "core/ocr/SystemOcrEngine.h"

#include <QtConcurrent>
#include <QElapsedTimer>
#include <QImage>

#ifdef XT_HAVE_WINRT
#  include <winrt/Windows.Foundation.h>
#  include <winrt/Windows.Foundation.Collections.h>
#  include <winrt/Windows.Globalization.h>
#  include <winrt/Windows.Graphics.Imaging.h>
#  include <winrt/Windows.Media.Ocr.h>
#  include <winrt/Windows.Storage.Streams.h>
#endif

#include <cstring>

SystemOcrEngine::SystemOcrEngine() = default;
SystemOcrEngine::~SystemOcrEngine() = default;

bool SystemOcrEngine::isAvailable()
{
#ifdef XT_HAVE_WINRT
    try {
        winrt::init_apartment();
        auto langs = winrt::Windows::Media::Ocr::OcrEngine::AvailableRecognizerLanguages();
        return langs.Size() > 0;
    } catch (...) {
        return false;
    }
#else
    return false;
#endif
}

QString SystemOcrEngine::pickLanguageTag(const QString &hint)
{
#ifdef XT_HAVE_WINRT
    try {
        winrt::init_apartment();
        auto langs = winrt::Windows::Media::Ocr::OcrEngine::AvailableRecognizerLanguages();
        if (langs.Size() == 0)
            return QString();

        // 优先精确匹配（zh-CN），再前缀匹配（zh → zh-CN），再退化到第一个。
        const std::wstring want = hint.toStdWString();
        QString first;
        QString prefixMatch;
        const int dash = hint.indexOf(QLatin1Char('-'));
        const QString prefix = dash > 0 ? hint.left(dash).toLower() : hint.toLower();

        for (uint32_t i = 0; i < langs.Size(); ++i) {
            auto lang = langs.GetAt(i);
            const QString tag = QString::fromStdWString(lang.LanguageTag().c_str());
            if (i == 0)
                first = tag;
            if (tag.toStdWString() == want)
                return tag;
            if (!prefix.isEmpty() && tag.toLower().startsWith(prefix))
                prefixMatch = tag;
        }
        return prefixMatch.isEmpty() ? first : prefixMatch;
    } catch (...) {
        return QString();
    }
#else
    Q_UNUSED(hint);
    return QString();
#endif
}

OcrResult SystemOcrEngine::recognizeSync(const QImage &image)
{
    OcrResult result;
#ifdef XT_HAVE_WINRT
    QElapsedTimer t;
    t.start();

    try {
        winrt::init_apartment();

        if (image.isNull()) {
            result.error = QStringLiteral("null image");
            return result;
        }
        // QImage RGBA8888 -> SoftwareBitmap BGRA8（R<->B 交换）。
        // 直接写 Buffer.data() 指针，避免额外 vector 临时变量与 WinRT 头
        // 中 array_view / data() 等同名方法产生解析歧义。
        const QImage conv = image.convertToFormat(QImage::Format_RGBA8888);
        const int imgW = conv.width();
        const int imgH = conv.height();
        const uint32_t pixelBytes = uint32_t(imgW) * uint32_t(imgH) * 4;

        auto buffer = winrt::Windows::Storage::Streams::Buffer(pixelBytes);
        buffer.Length(pixelBytes);
        // Buffer.data() 返回 uint8_t*（C++/WinRT 通过 IBufferByteAccess 取出）。
        uint8_t *dst = buffer.data();
        const unsigned char *src = conv.constBits();
        for (int i = 0; i < imgW * imgH; ++i) {
            const size_t off = size_t(i) * 4;
            dst[off + 0] = src[off + 2]; // B
            dst[off + 1] = src[off + 1]; // G
            dst[off + 2] = src[off + 0]; // R
            dst[off + 3] = src[off + 3]; // A
        }

        using namespace winrt::Windows::Graphics::Imaging;
        SoftwareBitmap sb(BitmapPixelFormat::Bgra8, imgW, imgH, BitmapAlphaMode::Premultiplied);
        sb.CopyFromBuffer(buffer);

        auto langs = winrt::Windows::Media::Ocr::OcrEngine::AvailableRecognizerLanguages();
        if (langs.Size() == 0) {
            result.error = QStringLiteral("no ocr language installed");
            return result;
        }
        auto engine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(langs.GetAt(0));
        if (!engine) {
            result.error = QStringLiteral("TryCreateFromLanguage returned null");
            return result;
        }

        auto ocrRes = engine.RecognizeAsync(sb).get();
        auto lines = ocrRes.Lines();
        // WinRT OcrLine 不暴露像素坐标，quad 退化为整图外接矩形。
        const QPointF tl(0, 0), tr(imgW, 0), br(imgW, imgH), bl(0, imgH);
        for (uint32_t i = 0; i < lines.Size(); ++i) {
            auto line = lines.GetAt(i);
            OcrLine ol;
            ol.quad << tl << tr << br << bl;
            ol.text = QString::fromStdWString(line.Text().c_str());
            ol.confidence = 1.0f; // 系统 OCR 不暴露 per-line confidence
            result.lines.append(ol);
        }
        result.elapsedMs = t.elapsed();
    } catch (const winrt::hresult_error &e) {
        result.error = QStringLiteral("winrt: %1 (0x%2)")
                           .arg(QString::fromStdWString(e.message().c_str()))
                           .arg(static_cast<quint32>(e.code()), 8, 16, QLatin1Char('0'));
    } catch (...) {
        result.error = QStringLiteral("unknown winrt exception");
    }
#else
    Q_UNUSED(image);
    result.error = QStringLiteral("winrt unavailable at build time");
#endif
    return result;
}

QFuture<OcrResult> SystemOcrEngine::recognize(const QImage &image)
{
    return QtConcurrent::run([this, image]() { return recognizeSync(image); });
}
