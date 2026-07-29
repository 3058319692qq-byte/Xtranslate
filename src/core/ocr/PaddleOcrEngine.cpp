// PaddleOcrEngine implementation.
//
// Pipeline:
//   det (DBNet)  : resize (long side <=960, /32 aligned) -> normalize
//                  (x/255-mean)/std -> NCHW -> prob map -> binarize@0.3 ->
//                  contours -> contour-mask box-score filter@0.6 ->
//                  minAreaRect -> unclip(ratio=1.6, dist=area*ratio/perimeter)
//                  -> map back to original coords -> drop min-side < 3px.
//   rec (CTC)    : perspective-crop each quad (rotate 90 if h > 1.5*w) ->
//                  aspect-sorted batches (<=6) -> per batch
//                  imgW = ceil(48 * max_wh_ratio) (safety cap 4096, no fixed
//                  320 truncation; rec input is fully dynamic [-1,3,48,-1]) ->
//                  each crop resized to (48, round(48*own ratio)) then
//                  right-padded with normalized zeros -> (x/127.5-1) ->
//                  greedy CTC decode with ppocrv6_dict.txt
//                  (index0=blank, trailing space char).
// Optional debug dump (setDebugDumpPrefix): det prob map / binary map /
// candidate-box overlay / per-line rec crops as PNGs.

#include "core/ocr/PaddleOcrEngine.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QMutexLocker>
#include <QTextStream>
#include <QtConcurrent/QtConcurrent>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {

constexpr int   kDetMaxSide       = 960;
constexpr float kDetBinThresh     = 0.3f;
constexpr float kDetBoxThresh     = 0.6f;   // contour-mask mean; 0.6 vs 0.5
                                            // A/B result in phase1_dev_fix1.md
constexpr float kDetUnclipRatio   = 1.6f;   // 1.5 clipped en head/tail glyphs
constexpr float kDetMinSidePx     = 3.0f;
constexpr int   kRecImgH          = 48;
constexpr int   kRecImgMaxW       = 4096;   // safety cap only (abnormal boxes);
                                            // rec input is dynamic [-1,3,48,-1]
constexpr int   kRecBatch         = 6;
constexpr float kRecDropThresh    = 0.5f;

QString modelDir()
{
    return QCoreApplication::applicationDirPath()
           + QStringLiteral("/models/paddleocr/pp-ocrv6-small/");
}

// QImage -> deep-copied 3-channel BGR cv::Mat.
cv::Mat qimageToBgr(const QImage &image)
{
    QImage img = image.convertToFormat(QImage::Format_BGR888);
    cv::Mat mat(img.height(), img.width(), CV_8UC3,
                const_cast<uchar *>(img.constBits()),
                static_cast<size_t>(img.bytesPerLine()));
    return mat.clone();
}

// Order 4 points as TL, TR, BR, BL.
std::vector<cv::Point2f> orderQuad(const cv::Point2f pts[4])
{
    std::vector<cv::Point2f> v(pts, pts + 4);
    std::vector<cv::Point2f> out(4);
    auto sum  = [](const cv::Point2f &p) { return p.x + p.y; };
    auto diff = [](const cv::Point2f &p) { return p.x - p.y; };
    out[0] = *std::min_element(v.begin(), v.end(),
                               [&](auto &a, auto &b) { return sum(a) < sum(b); });   // TL
    out[2] = *std::max_element(v.begin(), v.end(),
                               [&](auto &a, auto &b) { return sum(a) < sum(b); });   // BR
    out[1] = *std::max_element(v.begin(), v.end(),
                               [&](auto &a, auto &b) { return diff(a) < diff(b); }); // TR
    out[3] = *std::min_element(v.begin(), v.end(),
                               [&](auto &a, auto &b) { return diff(a) < diff(b); }); // BL
    return out;
}

// Mean probability inside the detected contour polygon on the (resized-scale)
// prob map. Using the exact contour mask (instead of the minAreaRect quad)
// keeps low-prob background between words/strokes of latin lines from
// dragging the score below kDetBoxThresh.
float boxScore(const cv::Mat &prob, const std::vector<cv::Point> &contour)
{
    cv::Rect bounds = cv::boundingRect(contour) & cv::Rect(0, 0, prob.cols, prob.rows);
    if (bounds.width <= 0 || bounds.height <= 0)
        return 0.f;
    cv::Mat mask = cv::Mat::zeros(bounds.size(), CV_8UC1);
    std::vector<cv::Point> shifted;
    shifted.reserve(contour.size());
    for (const auto &p : contour)
        shifted.emplace_back(p.x - bounds.x, p.y - bounds.y);
    cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{shifted}, cv::Scalar(1));
    return static_cast<float>(cv::mean(prob(bounds), mask)[0]);
}

// Expand a rotated rect along its own width/height axes by
// dist = area * ratio / perimeter on each side (Clipper-free unclip).
cv::RotatedRect unclipRect(const cv::RotatedRect &rect, float ratio)
{
    const float w = rect.size.width, h = rect.size.height;
    const float perimeter = 2.f * (w + h);
    if (perimeter <= 1e-3f)
        return rect;
    const float dist = w * h * ratio / perimeter;
    return cv::RotatedRect(rect.center,
                           cv::Size2f(w + 2.f * dist, h + 2.f * dist),
                           rect.angle);
}

// Perspective-crop one quad out of the BGR source image.
cv::Mat cropQuad(const cv::Mat &bgr, const QPolygonF &quad)
{
    cv::Point2f src[4];
    for (int i = 0; i < 4; ++i)
        src[i] = cv::Point2f(static_cast<float>(quad[i].x()),
                             static_cast<float>(quad[i].y()));
    const float wTop = static_cast<float>(cv::norm(src[0] - src[1]));
    const float wBot = static_cast<float>(cv::norm(src[3] - src[2]));
    const float hLft = static_cast<float>(cv::norm(src[0] - src[3]));
    const float hRgt = static_cast<float>(cv::norm(src[1] - src[2]));
    const int w = std::max(1, static_cast<int>(std::round(std::max(wTop, wBot))));
    const int h = std::max(1, static_cast<int>(std::round(std::max(hLft, hRgt))));

    cv::Point2f dst[4] = {
        {0.f, 0.f}, {static_cast<float>(w), 0.f},
        {static_cast<float>(w), static_cast<float>(h)}, {0.f, static_cast<float>(h)}
    };
    cv::Mat m = cv::getPerspectiveTransform(src, dst);
    cv::Mat out;
    cv::warpPerspective(bgr, out, m, cv::Size(w, h),
                        cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    if (out.rows > out.cols * 1.5)
        cv::rotate(out, out, cv::ROTATE_90_COUNTERCLOCKWISE);
    return out;
}

QString shapeToString(const std::vector<int64_t> &shape)
{
    QStringList parts;
    for (int64_t d : shape)
        parts << QString::number(d);
    return QStringLiteral("[%1]").arg(parts.join(QLatin1Char(',')));
}

// ---- --debug dump helpers (PNG via QImage; opencv imgcodecs not built) ----

bool saveGray8Png(const cv::Mat &m, const QString &path) // CV_8UC1
{
    QImage img(m.data, m.cols, m.rows, static_cast<qsizetype>(m.step),
               QImage::Format_Grayscale8);
    return img.copy().save(path, "PNG");
}

bool saveBgrPng(const cv::Mat &m, const QString &path) // CV_8UC3 BGR
{
    QImage img(m.data, m.cols, m.rows, static_cast<qsizetype>(m.step),
               QImage::Format_BGR888);
    return img.copy().save(path, "PNG");
}

struct DetDebugCandidate {
    std::vector<cv::Point> contour; // prob-map coords
    float score = 0.f;              // contour-mask mean probability
    bool kept = false;              // survived score + min-side filters
};

// Writes <prefix>_det_prob.png / _det_binary.png / _det_boxes.png.
// Overlay: kept candidates green, rejected red (score annotated), final
// unclipped quads blue - all in original-image coordinates.
void dumpDetDebug(const QString &prefix, const cv::Mat &prob, const cv::Mat &binary,
                  const cv::Mat &bgr, const std::vector<DetDebugCandidate> &cands,
                  const QVector<QPolygonF> &quads, float sx, float sy)
{
    cv::Mat prob8;
    prob.convertTo(prob8, CV_8UC1, 255.0);
    saveGray8Png(prob8, prefix + QStringLiteral("_det_prob.png"));
    saveGray8Png(binary, prefix + QStringLiteral("_det_binary.png"));

    cv::Mat overlay = bgr.clone();
    for (const auto &c : cands) {
        std::vector<cv::Point> pts;
        pts.reserve(c.contour.size());
        for (const auto &p : c.contour)
            pts.emplace_back(static_cast<int>(std::round(p.x * sx)),
                             static_cast<int>(std::round(p.y * sy)));
        const cv::Scalar color = c.kept ? cv::Scalar(0, 200, 0) : cv::Scalar(0, 0, 255);
        cv::polylines(overlay, std::vector<std::vector<cv::Point>>{pts}, true, color, 2);
        if (!pts.empty()) {
            cv::putText(overlay, cv::format("%.3f", c.score),
                        pts.front() + cv::Point(0, -4),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 1, cv::LINE_AA);
        }
    }
    for (const QPolygonF &quad : quads) {
        std::vector<cv::Point> pts;
        pts.reserve(quad.size());
        for (const QPointF &p : quad)
            pts.emplace_back(static_cast<int>(std::round(p.x())),
                             static_cast<int>(std::round(p.y())));
        cv::polylines(overlay, std::vector<std::vector<cv::Point>>{pts}, true,
                      cv::Scalar(255, 0, 0), 1);
    }
    saveBgrPng(overlay, prefix + QStringLiteral("_det_boxes.png"));
}

} // namespace

PaddleOcrEngine::PaddleOcrEngine()
    : m_env(ORT_LOGGING_LEVEL_ERROR, "XTranslateOcr")
{
    m_sessionOptions.SetIntraOpNumThreads(2);
    m_sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
}

PaddleOcrEngine::~PaddleOcrEngine() = default;

PaddleOcrEngine::SessionIo PaddleOcrEngine::reflectIo(Ort::Session &session)
{
    SessionIo io;
    Ort::AllocatorWithDefaultOptions alloc;
    const size_t nIn = session.GetInputCount();
    for (size_t i = 0; i < nIn; ++i) {
        io.inputNames.push_back(session.GetInputNameAllocated(i, alloc).get());
        io.inputShapes.push_back(
            session.GetInputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape());
    }
    const size_t nOut = session.GetOutputCount();
    for (size_t i = 0; i < nOut; ++i) {
        io.outputNames.push_back(session.GetOutputNameAllocated(i, alloc).get());
        io.outputShapes.push_back(
            session.GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape());
    }
    return io;
}

bool PaddleOcrEngine::ensureLoaded(QString *error)
{
    QMutexLocker lock(&m_mutex);
    if (m_loaded) {
        if (error) *error = m_loadError;
        return m_loadError.isEmpty();
    }
    m_loaded = true; // only attempt once; error is cached

    const QString dir = modelDir();
    const QString detPath  = dir + QStringLiteral("PP-OCRv6_small_det.onnx");
    const QString recPath  = dir + QStringLiteral("PP-OCRv6_small_rec.onnx");
    const QString dictPath = dir + QStringLiteral("ppocrv6_dict.txt");

    for (const QString &p : {detPath, recPath, dictPath}) {
        if (!QFile::exists(p)) {
            m_loadError = QStringLiteral("model file missing: %1").arg(p);
            if (error) *error = m_loadError;
            return false;
        }
    }

    try {
        m_det = std::make_unique<Ort::Session>(m_env, detPath.toStdWString().c_str(),
                                               m_sessionOptions);
        m_rec = std::make_unique<Ort::Session>(m_env, recPath.toStdWString().c_str(),
                                               m_sessionOptions);
        m_detIo = reflectIo(*m_det);
        m_recIo = reflectIo(*m_rec);
    } catch (const Ort::Exception &e) {
        m_loadError = QStringLiteral("onnxruntime session error: %1")
                          .arg(QString::fromUtf8(e.what()));
        if (error) *error = m_loadError;
        return false;
    }

    // Dictionary: line i (0-based) -> class i+1 (class 0 = CTC blank).
    // PaddleOCR use_space_char convention appends a literal space at the end.
    QFile f(dictPath);
    if (!f.open(QIODevice::ReadOnly)) {
        m_loadError = QStringLiteral("cannot open dict: %1").arg(dictPath);
        if (error) *error = m_loadError;
        return false;
    }
    m_dict.clear();
    QTextStream ts(&f);
    while (!ts.atEnd()) {
        QString line = ts.readLine();
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        m_dict << line; // keep empty interior lines to preserve class indices
    }
    // Strip a single trailing empty line produced by a final '\n'.
    if (!m_dict.isEmpty() && m_dict.last().isEmpty())
        m_dict.removeLast();
    m_dict << QStringLiteral(" ");

    if (error) *error = QString();
    return true;
}

QStringList PaddleOcrEngine::tensorInfo()
{
    QString err;
    if (!ensureLoaded(&err))
        return QStringList() << QStringLiteral("load error: %1").arg(err);

    QMutexLocker lock(&m_mutex);
    QStringList out;
    auto dump = [&out](const QString &tag, const SessionIo &io) {
        for (size_t i = 0; i < io.inputNames.size(); ++i)
            out << QStringLiteral("%1 input  %2 : %3")
                       .arg(tag, QString::fromStdString(io.inputNames[i]),
                            shapeToString(io.inputShapes[i]));
        for (size_t i = 0; i < io.outputNames.size(); ++i)
            out << QStringLiteral("%1 output %2 : %3")
                       .arg(tag, QString::fromStdString(io.outputNames[i]),
                            shapeToString(io.outputShapes[i]));
    };
    dump(QStringLiteral("det"), m_detIo);
    dump(QStringLiteral("rec"), m_recIo);
    out << QStringLiteral("dict classes : %1 (blank + %2 dict lines incl. appended space)")
               .arg(m_dict.size() + 1).arg(m_dict.size());
    return out;
}

QFuture<OcrResult> PaddleOcrEngine::recognize(const QImage &image)
{
    const QImage copy = image; // implicit share is fine; detach happens in worker
    return QtConcurrent::run([this, copy]() { return recognizeSync(copy); });
}

OcrResult PaddleOcrEngine::recognizeSync(const QImage &image)
{
    OcrResult result;
    QElapsedTimer timer;
    timer.start();

    if (image.isNull() || image.width() < 3 || image.height() < 3) {
        result.error = QStringLiteral("input image is null or too small");
        result.elapsedMs = timer.elapsed();
        return result;
    }
    if (!ensureLoaded(&result.error)) {
        result.elapsedMs = timer.elapsed();
        return result;
    }

    QMutexLocker lock(&m_mutex);
    try {
        const cv::Mat bgr = qimageToBgr(image);
        const int origW = bgr.cols, origH = bgr.rows;

        // ---- det preprocess -------------------------------------------------
        float scale = 1.f;
        const int longSide = std::max(origW, origH);
        if (longSide > kDetMaxSide)
            scale = static_cast<float>(kDetMaxSide) / longSide;
        int dw = std::max(32, static_cast<int>(std::round(origW * scale / 32.f)) * 32);
        int dh = std::max(32, static_cast<int>(std::round(origH * scale / 32.f)) * 32);
        dw = std::min(dw, kDetMaxSide);
        dh = std::min(dh, kDetMaxSide);

        cv::Mat resized;
        cv::resize(bgr, resized, cv::Size(dw, dh));
        cv::Mat rgb;
        cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);

        static const float mean[3] = {0.485f, 0.456f, 0.406f};
        static const float stdv[3] = {0.229f, 0.224f, 0.225f};
        std::vector<float> detInput(static_cast<size_t>(3) * dh * dw);
        {
            const int plane = dh * dw;
            for (int y = 0; y < dh; ++y) {
                const uchar *row = rgb.ptr<uchar>(y);
                for (int x = 0; x < dw; ++x) {
                    for (int c = 0; c < 3; ++c) {
                        detInput[static_cast<size_t>(c) * plane + y * dw + x] =
                            (row[x * 3 + c] / 255.f - mean[c]) / stdv[c];
                    }
                }
            }
        }

        // ---- det inference ---------------------------------------------------
        Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,
                                                             OrtMemTypeDefault);
        const std::array<int64_t, 4> detShape{1, 3, dh, dw};
        Ort::Value detTensor = Ort::Value::CreateTensor<float>(
            memInfo, detInput.data(), detInput.size(),
            detShape.data(), detShape.size());

        const char *detInName  = m_detIo.inputNames[0].c_str();
        const char *detOutName = m_detIo.outputNames[0].c_str();
        auto detOut = m_det->Run(Ort::RunOptions{nullptr},
                                 &detInName, &detTensor, 1, &detOutName, 1);

        const auto detOutShape =
            detOut[0].GetTensorTypeAndShapeInfo().GetShape(); // [1,1,H,W]
        const int mh = static_cast<int>(detOutShape[detOutShape.size() - 2]);
        const int mw = static_cast<int>(detOutShape[detOutShape.size() - 1]);
        cv::Mat prob(mh, mw, CV_32FC1,
                     const_cast<float *>(detOut[0].GetTensorData<float>()));

        // ---- det postprocess (DBNet) ------------------------------------------
        cv::Mat binary;
        cv::threshold(prob, binary, kDetBinThresh, 1.0, cv::THRESH_BINARY);
        binary.convertTo(binary, CV_8UC1, 255);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(binary, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

        const float sx = static_cast<float>(origW) / mw;
        const float sy = static_cast<float>(origH) / mh;

        const bool dbg = !m_debugPrefix.isEmpty();
        std::vector<DetDebugCandidate> dbgCandidates;

        QVector<QPolygonF> quads;
        for (const auto &contour : contours) {
            if (contour.size() < 3)
                continue;
            cv::RotatedRect rect = cv::minAreaRect(contour);
            if (std::min(rect.size.width, rect.size.height) < 1.f)
                continue;

            const float score = boxScore(prob, contour);
            bool kept = score >= kDetBoxThresh;
            if (kept) {
                cv::RotatedRect expanded = unclipRect(rect, kDetUnclipRatio);
                cv::Point2f pts[4];
                expanded.points(pts);
                const auto ordered = orderQuad(pts);

                const float minSide = std::min(expanded.size.width, expanded.size.height)
                                      * std::min(sx, sy);
                if (minSide < kDetMinSidePx) {
                    kept = false;
                } else {
                    QPolygonF quad;
                    for (const auto &p : ordered) {
                        quad << QPointF(std::clamp(p.x * sx, 0.f, static_cast<float>(origW - 1)),
                                        std::clamp(p.y * sy, 0.f, static_cast<float>(origH - 1)));
                    }
                    quads << quad;
                }
            }
            if (dbg)
                dbgCandidates.push_back({contour, score, kept});
        }

        if (dbg)
            dumpDetDebug(m_debugPrefix, prob, binary, bgr, dbgCandidates, quads, sx, sy);

        if (quads.isEmpty()) {
            result.elapsedMs = timer.elapsed();
            return result; // no text found - not an error
        }

        // ---- rec preprocess ---------------------------------------------------
        // PaddleOCR-style dynamic width (no fixed 320 cap - the old cap
        // squeezed long latin lines horizontally and dropped characters):
        // sort crops by aspect ratio, then per batch use
        // imgW = ceil(48 * max_wh_ratio) (safety cap kRecImgMaxW); each sample
        // is resized to (48, round(48 * own wh_ratio)) and right-padded with
        // zeros in normalized space. rec input is fully dynamic [-1,3,48,-1].
        struct RecItem {
            int srcIndex = 0;
            cv::Mat crop;        // BGR perspective crop (original resolution)
            float whRatio = 1.f; // crop w/h after optional 90-deg rotation
            QString text;
            float confidence = 0.f;
        };
        std::vector<RecItem> items;
        items.reserve(quads.size());
        for (int i = 0; i < quads.size(); ++i) {
            RecItem item;
            item.srcIndex = i;
            item.crop = cropQuad(bgr, quads[i]);
            item.whRatio = item.crop.rows > 0
                ? static_cast<float>(item.crop.cols) / item.crop.rows : 1.f;
            items.push_back(std::move(item));
        }
        // Sort by aspect ratio so batches share similar widths (less padding).
        std::sort(items.begin(), items.end(),
                  [](const RecItem &a, const RecItem &b) { return a.whRatio < b.whRatio; });

        const int numClasses = m_dict.size() + 1; // + blank
        const char *recInName  = m_recIo.inputNames[0].c_str();
        const char *recOutName = m_recIo.outputNames[0].c_str();

        for (size_t start = 0; start < items.size(); start += kRecBatch) {
            const size_t end = std::min(items.size(), start + kRecBatch);
            const int batch = static_cast<int>(end - start);
            float maxWhRatio = 1.f;
            for (size_t i = start; i < end; ++i)
                maxWhRatio = std::max(maxWhRatio, items[i].whRatio);
            const int imgW = std::min(kRecImgMaxW, static_cast<int>(
                std::ceil(kRecImgH * static_cast<double>(maxWhRatio))));

            const size_t plane = static_cast<size_t>(kRecImgH) * imgW;
            std::vector<float> recInput(static_cast<size_t>(batch) * 3 * plane, 0.f);
            for (size_t i = start; i < end; ++i) {
                int w = static_cast<int>(std::round(
                    kRecImgH * static_cast<double>(items[i].whRatio)));
                w = std::clamp(w, 8, imgW);
                cv::Mat scaled; // BGR; rec normalization is channel-symmetric
                cv::resize(items[i].crop, scaled, cv::Size(w, kRecImgH));
                if (dbg) {
                    saveBgrPng(scaled, m_debugPrefix
                               + QStringLiteral("_rec_crop_%1.png")
                                     .arg(items[i].srcIndex, 2, 10, QLatin1Char('0')));
                }
                const size_t base = (i - start) * 3 * plane;
                for (int y = 0; y < kRecImgH; ++y) {
                    const uchar *row = scaled.ptr<uchar>(y);
                    for (int x = 0; x < scaled.cols; ++x) {
                        for (int c = 0; c < 3; ++c) {
                            recInput[base + static_cast<size_t>(c) * plane
                                     + static_cast<size_t>(y) * imgW + x] =
                                row[x * 3 + c] / 127.5f - 1.f;
                        }
                    }
                }
            }

            const std::array<int64_t, 4> recShape{batch, 3, kRecImgH, imgW};
            Ort::Value recTensor = Ort::Value::CreateTensor<float>(
                memInfo, recInput.data(), recInput.size(),
                recShape.data(), recShape.size());
            auto recOut = m_rec->Run(Ort::RunOptions{nullptr},
                                     &recInName, &recTensor, 1, &recOutName, 1);

            const auto outShape =
                recOut[0].GetTensorTypeAndShapeInfo().GetShape(); // [B,T,C]
            const int T = static_cast<int>(outShape[1]);
            const int C = static_cast<int>(outShape[2]);
            const float *data = recOut[0].GetTensorData<float>();

            // Detect whether the head already applies softmax.
            bool isProb = false;
            {
                double s = 0.0;
                for (int c = 0; c < C; ++c)
                    s += data[c];
                isProb = std::abs(s - 1.0) < 1e-2;
            }

            for (size_t i = start; i < end; ++i) {
                const float *seq = data + (i - start) * static_cast<size_t>(T) * C;
                QString text;
                double confSum = 0.0;
                int confCnt = 0;
                int prevIdx = 0;
                for (int t = 0; t < T; ++t) {
                    const float *row = seq + static_cast<size_t>(t) * C;
                    int best = 0;
                    float bestV = row[0];
                    for (int c = 1; c < C; ++c) {
                        if (row[c] > bestV) { bestV = row[c]; best = c; }
                    }
                    float p = bestV;
                    if (!isProb) { // softmax the winning logit
                        double denom = 0.0;
                        for (int c = 0; c < C; ++c)
                            denom += std::exp(static_cast<double>(row[c]) - bestV);
                        p = static_cast<float>(1.0 / denom);
                    }
                    if (best != 0 && best != prevIdx) { // collapse repeats, skip blank
                        if (best - 1 < m_dict.size()) {
                            text += m_dict.at(best - 1);
                            confSum += p;
                            ++confCnt;
                        }
                    }
                    prevIdx = best;
                }
                items[i].text = text;
                items[i].confidence =
                    confCnt > 0 ? static_cast<float>(confSum / confCnt) : 0.f;
            }
            Q_UNUSED(numClasses);
        }

        // ---- assemble in original detection order, then sort by y, x ----------
        QVector<OcrLine> lines(quads.size());
        for (const auto &item : items) {
            OcrLine line;
            line.quad = quads[item.srcIndex];
            line.text = item.text;
            line.confidence = item.confidence;
            lines[item.srcIndex] = line;
        }
        for (const OcrLine &line : lines) {
            if (line.text.trimmed().isEmpty() || line.confidence < kRecDropThresh)
                continue;
            result.lines << line;
        }
        std::sort(result.lines.begin(), result.lines.end(),
                  [](const OcrLine &a, const OcrLine &b) {
                      const QPointF pa = a.quad.value(0), pb = b.quad.value(0);
                      if (std::abs(pa.y() - pb.y()) > 1e-3)
                          return pa.y() < pb.y();
                      return pa.x() < pb.x();
                  });
    } catch (const Ort::Exception &e) {
        result.error = QStringLiteral("onnxruntime inference error: %1")
                           .arg(QString::fromUtf8(e.what()));
    } catch (const cv::Exception &e) {
        result.error = QStringLiteral("opencv error: %1")
                           .arg(QString::fromUtf8(e.what()));
    } catch (const std::exception &e) {
        result.error = QStringLiteral("ocr error: %1").arg(QString::fromUtf8(e.what()));
    }

    result.elapsedMs = timer.elapsed();
    return result;
}
