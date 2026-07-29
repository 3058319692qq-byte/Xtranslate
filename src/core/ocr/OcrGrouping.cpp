#include "core/ocr/OcrGrouping.h"

#include <algorithm>

namespace {

constexpr qreal kGapFactor = 0.6;      // vertical gap < median height * 0.6
constexpr qreal kMinOverlap = 0.3;     // horizontal overlap ratio > 30%

// overlap width / min(width) of the two boxes; 0 when disjoint.
qreal horizontalOverlapRatio(const QRectF &a, const QRectF &b)
{
    const qreal overlap = qMin(a.right(), b.right()) - qMax(a.left(), b.left());
    const qreal minWidth = qMin(a.width(), b.width());
    if (overlap <= 0.0 || minWidth <= 0.0)
        return 0.0;
    return overlap / minWidth;
}

} // namespace

QVector<OcrGroup> groupOcrLines(const QVector<OcrLine> &lines)
{
    struct Entry {
        QRectF box;
        QString text;
    };

    QVector<Entry> entries;
    entries.reserve(lines.size());
    QVector<qreal> heights;
    heights.reserve(lines.size());
    for (const OcrLine &line : lines) {
        const QRectF box = line.quad.boundingRect();
        if (box.isEmpty() || line.text.trimmed().isEmpty())
            continue;
        entries.append({box, line.text});
        heights.append(box.height());
    }
    if (entries.isEmpty())
        return {};

    // Median line height over the whole result -> the vertical gap threshold.
    QVector<qreal> sortedHeights = heights;
    std::sort(sortedHeights.begin(), sortedHeights.end());
    const qreal medianHeight = sortedHeights.at(sortedHeights.size() / 2);
    const qreal maxGap = medianHeight * kGapFactor;

    std::stable_sort(entries.begin(), entries.end(),
                     [](const Entry &a, const Entry &b) {
                         return a.box.top() < b.box.top();
                     });

    QVector<OcrGroup> groups;
    QRectF lastBox; // last member of the currently open group
    for (const Entry &e : entries) {
        bool merged = false;
        if (!groups.isEmpty()) {
            const qreal gap = e.box.top() - lastBox.bottom();
            if (gap < maxGap
                && horizontalOverlapRatio(e.box, lastBox) > kMinOverlap) {
                OcrGroup &g = groups.last();
                g.bbox = g.bbox.united(e.box);
                g.texts.append(e.text);
                g.lineHeight += e.box.height();
                merged = true;
            }
        }
        if (!merged) {
            OcrGroup g;
            g.bbox = e.box;
            g.texts.append(e.text);
            g.lineHeight = e.box.height();
            groups.append(g);
        }
        lastBox = e.box;
    }

    for (OcrGroup &g : groups)
        g.lineHeight /= g.texts.size(); // accumulated sum -> mean

    return groups;
}
