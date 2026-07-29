// PaddleOcrEngine - PP-OCRv6 (det + rec) on ONNX Runtime.
//
// Model files are loaded from:
//   <applicationDirPath>/models/paddleocr/pp-ocrv6-small/
//     PP-OCRv6_small_det.onnx   (DBNet text detector)
//     PP-OCRv6_small_rec.onnx   (CTC text recognizer)
//     ppocrv6_dict.txt          (one glyph per line, UTF-8)
//
// Ort::Env / Ort::Session are created once and reused; a QMutex serializes
// session access. recognize() dispatches to QtConcurrent.

#pragma once

#include "core/ocr/OcrEngine.h"

#include <QMutex>
#include <QStringList>

#include <memory>
#include <vector>

#include <onnxruntime_cxx_api.h>

class PaddleOcrEngine : public OcrEngine
{
public:
    PaddleOcrEngine();
    ~PaddleOcrEngine() override;

    QFuture<OcrResult> recognize(const QImage &image) override;

    // Lazily loads both sessions + dict; returns false (with error message)
    // when any model file is missing or fails to load.
    bool ensureLoaded(QString *error = nullptr);

    // Human-readable "name : shape" description of every input/output tensor,
    // discovered via Ort reflection (used by --selftest and the dev report).
    QStringList tensorInfo();

    // Debug dump for --selftest ocr --debug: when a non-empty prefix is set,
    // recognizeSync() writes "<prefix>_det_prob.png", "<prefix>_det_binary.png",
    // "<prefix>_det_boxes.png" (candidates + scores overlay) and one
    // "<prefix>_rec_crop_NN.png" per detected line. Set before recognize().
    void setDebugDumpPrefix(const QString &prefix) { m_debugPrefix = prefix; }

private:
    struct SessionIo {
        std::vector<std::string> inputNames;
        std::vector<std::string> outputNames;
        std::vector<std::vector<int64_t>> inputShapes;
        std::vector<std::vector<int64_t>> outputShapes;
    };

    OcrResult recognizeSync(const QImage &image);
    static SessionIo reflectIo(Ort::Session &session);

    Ort::Env m_env;
    Ort::SessionOptions m_sessionOptions;
    std::unique_ptr<Ort::Session> m_det;
    std::unique_ptr<Ort::Session> m_rec;
    SessionIo m_detIo;
    SessionIo m_recIo;
    QStringList m_dict;      // index 0 = blank; dict line i -> class i+1; " " appended
    QString m_debugPrefix;   // empty = no debug dump
    QMutex m_mutex;          // guards session usage + lazy init
    bool m_loaded = false;
    QString m_loadError;
};
