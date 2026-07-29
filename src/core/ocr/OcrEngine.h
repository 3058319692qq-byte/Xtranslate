// OcrEngine - abstract OCR interface.
//
// Implementations run recognition asynchronously and report all line boxes in
// original-image coordinates.

#pragma once

#include <QFuture>
#include <QImage>
#include <QPolygonF>
#include <QString>
#include <QVector>

struct OcrLine {
    QPolygonF quad;          // 4 vertices in original-image coords (TL,TR,BR,BL)
    QString text;
    float confidence = 0.f;  // mean per-char probability, [0,1]
};

struct OcrResult {
    QVector<OcrLine> lines;
    qint64 elapsedMs = 0;
    QString error;           // empty on success
};

class OcrEngine
{
public:
    virtual ~OcrEngine() = default;

    // Thread-safe; the returned future completes on a worker thread.
    virtual QFuture<OcrResult> recognize(const QImage &image) = 0;
};
