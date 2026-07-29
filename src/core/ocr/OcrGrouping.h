// OcrGrouping - merge adjacent OCR lines into paragraph groups.
//
// Rule (phase 2 spec): sort lines by their quad's top edge; a line joins the
// previous group when
//   * vertical gap to the group's last line  <  median(line height) * 0.6, and
//   * horizontal overlap ratio               >  30%
//     (overlap width / min(width of the two boxes)).
// Fewer translate requests and more coherent paragraphs as a result.

#pragma once

#include "core/ocr/OcrEngine.h"

#include <QRectF>
#include <QStringList>
#include <QVector>

struct OcrGroup {
    QRectF bbox;            // union of member boxes, image pixels
    QStringList texts;      // member line texts, top -> bottom
    qreal lineHeight = 0.0; // mean member line height, image pixels

    QString joinedText() const { return texts.join(QLatin1Char('\n')); }
};

QVector<OcrGroup> groupOcrLines(const QVector<OcrLine> &lines);
