// ScreenTranslateController - the "截图翻译" pipeline glue.
//
// Owns one OverlayWindow + one ControlBar for a captured region and drives
//   grab region -> OCR -> group lines -> translate each group concurrently
//   -> update the overlay as results arrive.
// The ControlBar's 重译 re-grabs the SAME region m_region (overlay + bar are
// hidden during the grab so we never capture ourselves; a grip drag may have
// parked either window over the original region) and reruns the pipeline;
// 关闭 tears everything down and emits finished().
// Phase 7-fix2b：拖动（grip）只平移两个窗口的显示位置，m_region 永不改变——
// 拖动改变"译文显示在哪"，不改变"翻译的是哪块屏幕"。

#pragma once

#include "core/ocr/OcrEngine.h"
#include "core/ocr/OcrGrouping.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>

class ControlBar;
class OverlayWindow;

class ScreenTranslateController : public QObject
{
    Q_OBJECT
public:
    // `regionLogical`: global logical rect from RegionSelector.
    // from/to/provider: translation settings captured from the main window.
    ScreenTranslateController(const QRect &regionLogical,
                              const QString &from, const QString &to,
                              const QString &provider,
                              QObject *parent = nullptr);
    ~ScreenTranslateController() override;

    void start();

signals:
    void finished();          // overlay closed (or pipeline failed early)
    void statusMessage(const QString &message);
    // 截图翻译批处理落到 mock 兜底时发出，由 MainWindow 转发到托盘气泡。
    void fallbackToMock(const QString &message);

private:
    void runPipeline();       // grab + OCR (asynchronous)
    void onOcrFinished(const OcrResult &result);
    void translateGroups();
    void teardown();

    QRect m_region;           // global logical
    QString m_from;
    QString m_to;
    QString m_provider;

    OverlayWindow *m_overlay = nullptr;
    ControlBar *m_bar = nullptr;
    QVector<OcrGroup> m_groups;
    quint64 m_generation = 0; // bumped on 重译 to drop stale async results
    bool m_overlayVisible = true;
    // Phase 7-fix2b：grip 拖动起点的 overlay 位置。dragRequested 的 delta 是
    // 自拖动起点的累计位移，必须做"起点+delta"绝对定位（禁止增量叠加）。
    QPoint m_overlayDragStartPos;

    // Per-run translate bookkeeping so the LAST finished group can push a
    // final status ("provider · Nms" / failure) instead of leaving 翻译中….
    int m_pendingTranslations = 0;
    int m_failedTranslations = 0;
    QString m_lastProvider;
    QElapsedTimer m_translateTimer;
};
