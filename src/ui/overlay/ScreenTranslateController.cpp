#include "ui/overlay/ScreenTranslateController.h"

#include "core/capture/ScreenCapturer.h"
#include "core/ocr/OcrEngineFactory.h"
#include "core/storage/HistoryStore.h"
#include "core/translate/TranslationManager.h"
#include "ui/overlay/ControlBar.h"
#include "ui/overlay/OverlayWindow.h"

#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QFutureWatcher>
#include <QTimer>

ScreenTranslateController::ScreenTranslateController(const QRect &regionLogical,
                                                     const QString &from,
                                                     const QString &to,
                                                     const QString &provider,
                                                     QObject *parent)
    : QObject(parent)
    , m_region(regionLogical)
    , m_from(from)
    , m_to(to)
    , m_provider(provider)
{
}

ScreenTranslateController::~ScreenTranslateController()
{
    delete m_overlay;
    delete m_bar;
}

void ScreenTranslateController::start()
{
    m_overlay = new OverlayWindow(m_region);
    m_bar = new ControlBar;
    m_bar->dockTo(m_region);

    connect(m_bar, &ControlBar::retranslateRequested, this, [this]() {
        // Hide the overlay AND the bar so the re-grab captures the game, not
        // ourselves: after a grip drag either window may sit on top of the
        // ORIGINAL region (m_region), which is what runPipeline re-grabs.
        // 重译永远针对捕获时锁定的原选区 m_region——拖动只改"译文显示在哪"，
        // 不改变"翻译的是哪块屏幕"（BUG7 契约）。
        m_overlay->hide();
        m_bar->hide();
        emit statusMessage(tr("重新识别中…"));
        QTimer::singleShot(150, this, &ScreenTranslateController::runPipeline);
    });
    connect(m_bar, &ControlBar::copyRequested, this, [this]() {
        const QString text = m_overlay->allTranslatedText();
        if (!text.isEmpty()) {
            QApplication::clipboard()->setText(text);
            emit statusMessage(tr("译文已复制"));
        }
    });
    connect(m_bar, &ControlBar::toggleOverlayRequested, this, [this]() {
        m_overlayVisible = !m_overlayVisible;
        m_overlay->setVisible(m_overlayVisible);
        m_bar->setOverlayVisible(m_overlayVisible);
    });
    // Phase 7-fix2b BUG7/BUG8：grip 握把是唯一拖动入口（锁定/解锁已移除，
    // overlay 恒穿透）。dragRequested 的 delta 是自拖动起点的【累计】位移，
    // 必须用 dragStarted 时记录的起始位置做绝对定位（起点+delta）。
    // 回归教训（真机日志坐实）：此前写成 m_overlay->pos()+delta（增量叠加
    // 累计值），10 步 170px 的拖动把 overlay 从 128,151 推到 1063,1086
    // （Σ delta 复利），面板"拖完就消失"。禁止再改回增量式。
    // 拖动只 move() 窗口：m_region 与 OCR 分组数据一概不动（BUG7 契约）。
    connect(m_bar, &ControlBar::dragStarted, this, [this]() {
        if (m_overlay)
            m_overlayDragStartPos = m_overlay->pos();
    });
    connect(m_bar, &ControlBar::dragRequested, this, [this](const QPoint &delta) {
        if (m_overlay)
            m_overlay->move(m_overlayDragStartPos + delta);
    });
    connect(m_bar, &ControlBar::dragFinished, this, [this]() {
        if (!m_overlay)
            return;
        // 拖动结束强制重绘一次，确保分层窗口的最后一帧面板可见。
        m_overlay->update();
        qInfo().noquote() << QStringLiteral(
            "[stc] drag finished: overlay %1,%2 bar %3,%4 region %5,%6 %7x%8 (region unchanged)")
            .arg(m_overlay->pos().x()).arg(m_overlay->pos().y())
            .arg(m_bar->pos().x()).arg(m_bar->pos().y())
            .arg(m_region.x()).arg(m_region.y())
            .arg(m_region.width()).arg(m_region.height());
    });
    connect(m_bar, &ControlBar::closeRequested,
            this, &ScreenTranslateController::teardown);

    runPipeline();
}

void ScreenTranslateController::runPipeline()
{
    const quint64 generation = ++m_generation;

    const QImage frame = ScreenCapturer::grabLogicalRect(m_region);
    if (frame.isNull()) {
        emit statusMessage(tr("截屏失败"));
        teardown();
        return;
    }

    auto *watcher = new QFutureWatcher<OcrResult>(this);
    connect(watcher, &QFutureWatcher<OcrResult>::finished, this,
            [this, watcher, generation]() {
        watcher->deleteLater();
        if (generation != m_generation)
            return; // superseded by a newer 重译
        onOcrFinished(watcher->result());
    });
    watcher->setFuture(OcrEngineFactory::instance().engine()->recognize(frame));
    emit statusMessage(tr("识别中…"));
}

void ScreenTranslateController::onOcrFinished(const OcrResult &result)
{
    if (!result.error.isEmpty()) {
        emit statusMessage(tr("OCR 失败:%1").arg(result.error));
        teardown();
        return;
    }

    m_groups = groupOcrLines(result.lines);
    if (m_groups.isEmpty()) {
        emit statusMessage(tr("选区内未识别到文字"));
        teardown();
        return;
    }

    // Group boxes are in captured-image PHYSICAL pixels; the overlay places
    // panels in LOGICAL px relative to the region. One uniform scale factor
    // applies because the region lives on a single screen.
    const QRect physical = ScreenCapturer::logicalToPhysical(m_region);
    const qreal scale = m_region.width() > 0
        ? static_cast<qreal>(physical.width()) / m_region.width()
        : 1.0;

    QVector<OverlayGroup> overlayGroups;
    overlayGroups.reserve(m_groups.size());
    for (const OcrGroup &g : m_groups) {
        OverlayGroup og;
        og.rect = QRectF(g.bbox.x() / scale, g.bbox.y() / scale,
                         g.bbox.width() / scale, g.bbox.height() / scale);
        og.lineHeight = g.lineHeight / scale;
        og.sourceText = g.joinedText();
        og.loading = true;
        overlayGroups.append(og);
    }
    m_overlay->setGroups(overlayGroups);
    if (m_overlayVisible)
        m_overlay->show();
    m_bar->show();

    translateGroups();
}

void ScreenTranslateController::translateGroups()
{
    const quint64 generation = m_generation;
    emit statusMessage(tr("翻译中…(%1 组)").arg(m_groups.size()));

    m_pendingTranslations = m_groups.size();
    m_failedTranslations = 0;
    m_lastProvider.clear();
    m_translateTimer.start();

    // One translate call per group; the futures run/complete concurrently on
    // the main thread's event loop.
    for (int i = 0; i < m_groups.size(); ++i) {
        auto *watcher = new QFutureWatcher<ManagedTransResult>(this);
        connect(watcher, &QFutureWatcher<ManagedTransResult>::finished, this,
                [this, watcher, i, generation]() {
            watcher->deleteLater();
            if (generation != m_generation || !m_overlay)
                return;
            const ManagedTransResult r = watcher->result();
            const bool failed = !r.result.error.isEmpty();
            const QString text = !failed
                ? r.result.text
                : tr("[翻译失败] %1").arg(m_groups.at(i).joinedText());
            m_overlay->setGroupTranslation(i, text);

            // Final status once the whole batch settled.
            if (failed)
                ++m_failedTranslations;
            else
                m_lastProvider = r.result.provider;
            if (--m_pendingTranslations == 0) {
                if (m_failedTranslations == 0) {
                    if (m_lastProvider == QLatin1String("mock")) {
                        // 全部组落到 mock 兜底：状态栏加 ⚠ 前缀，并发出信号
                        // 让 MainWindow 弹托盘气泡引导用户检查代理。
                        emit statusMessage(
                            tr("⚠ 翻译服务不可达，已显示离线占位结果"));
                        emit fallbackToMock(
                            tr("⚠ 翻译服务不可达，已显示离线占位结果"));
                    } else {
                        emit statusMessage(QStringLiteral("%1 · %2 ms")
                                               .arg(m_lastProvider)
                                               .arg(m_translateTimer.elapsed()));
                    }
                } else {
                    emit statusMessage(
                        tr("翻译失败：%1/%2 组（%3）")
                            .arg(m_failedTranslations)
                            .arg(m_groups.size())
                            .arg(r.errorChain.join(QStringLiteral(" | "))));
                }

                // History scene 'capture': one row for the whole region
                // (groups joined) once the batch settled with any success.
                if (m_failedTranslations < m_groups.size()
                    && m_lastProvider != QLatin1String("mock")
                    && !m_lastProvider.isEmpty()) {
                    QStringList src;
                    for (const OcrGroup &g : m_groups)
                        src << g.joinedText();
                    HistoryEntry entry;
                    entry.srcLang = m_from;
                    entry.dstLang = m_to;
                    entry.srcText = src.join(QStringLiteral("\n"));
                    entry.dstText = m_overlay->allTranslatedText();
                    entry.provider = m_lastProvider;
                    entry.scene = QStringLiteral("capture");
                    HistoryStore::instance().add(entry);
                }
            }
        });
        watcher->setFuture(TranslationManager::instance().translate(
            m_groups.at(i).joinedText(), m_from, m_to, m_provider));
    }
}

void ScreenTranslateController::teardown()
{
    ++m_generation; // invalidate all in-flight watchers
    if (m_overlay)
        m_overlay->hide();
    if (m_bar)
        m_bar->hide();
    emit finished();
    deleteLater();
}
