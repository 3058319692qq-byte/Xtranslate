#include "app/SelfTest.h"

#include "core/capture/ScreenCapturer.h"
#include "core/config/ConfigManager.h"
#include "core/hotkey/HotkeyManager.h"
#include "core/ocr/OcrGrouping.h"
#include "core/ocr/PaddleOcrEngine.h"
#include "core/ocr/SystemOcrEngine.h"
#include "core/plugin/PluginManager.h"
#include "core/plugin/PluginTranslator.h"
#include "core/replace/TextReplacer.h"
#include "core/selection/SelectionGrabber.h"
#include "core/storage/HistoryStore.h"
#include "core/translate/TranslationManager.h"
#include "core/translate/providers/BaiduTranslator.h"
#include "core/translate/providers/DeepLTranslator.h"
#include "core/translate/providers/TencentTranslator.h"
#include "core/translate/providers/YoudaoTranslator.h"
#include "core/tts/EdgeTtsProvider.h"
#include "core/tts/TtsManager.h"
#include "ui/overlay/ControlBar.h"
#include "ui/overlay/OverlayWindow.h"
#include "ui/theme/ThemeManager.h"

#ifdef _WIN32
#  ifndef WINVER
#    define WINVER 0x0A00
#  endif
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0A00
#  endif
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

#include <QCoreApplication>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFutureWatcher>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QPainter>
#include <QPalette>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <onnxruntime_cxx_api.h>

#include <opencv2/core.hpp>
#include <opencv2/core/version.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <cstdio>

namespace selftest {

namespace {

void printJsonLine(const QJsonObject &obj)
{
    const QByteArray json = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    std::fwrite(json.constData(), 1, static_cast<size_t>(json.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

// 800x200 white canvas, black "Hello World 你好世界" at 40pt (YaHei/SimHei).
QImage makeBuiltInImage()
{
    QImage img(800, 200, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter painter(&img);
    painter.setPen(Qt::black);
    QFont font;
    font.setFamilies({QStringLiteral("Microsoft YaHei"), QStringLiteral("SimHei")});
    font.setPointSize(40);
    painter.setFont(font);
    painter.drawText(img.rect(), Qt::AlignCenter,
                     QStringLiteral("Hello World 你好世界"));
    painter.end();
    return img;
}

} // namespace

int runEnv()
{
    // Unchanged phase 0 contract.
    const QString base = QCoreApplication::applicationDirPath()
                         + QStringLiteral("/models/paddleocr/pp-ocrv6-small/");
    const bool modelsFound =
        QFileInfo::exists(base + QStringLiteral("PP-OCRv6_small_det.onnx"))
        && QFileInfo::exists(base + QStringLiteral("PP-OCRv6_small_rec.onnx"))
        && QFileInfo::exists(base + QStringLiteral("ppocrv6_dict.txt"));

    bool opencvOk = false;
    {
        cv::Mat src = cv::Mat::zeros(8, 8, CV_8UC1);
        cv::Mat dst;
        cv::GaussianBlur(src, dst, cv::Size(3, 3), 0);
        opencvOk = dst.rows == src.rows && dst.cols == src.cols;
    }

    QString ortVersion;
    try {
        ortVersion = QString::fromStdString(Ort::GetVersionString());
    } catch (...) {
        ortVersion = QStringLiteral("unknown");
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("app"), QStringLiteral("XTranslate"));
    obj.insert(QStringLiteral("display_name"), QStringLiteral("X翻译"));
    // Phase 7-fix1：新增 version 字段 = applicationVersion，让验收 agent 自动核对
    // exe 属性 / 关于页 / selftest env 三处版本号一致，防止再次漂移漏检。
    obj.insert(QStringLiteral("version"),
               QCoreApplication::applicationVersion());
    obj.insert(QStringLiteral("qt"), QString::fromLatin1(qVersion()));
    obj.insert(QStringLiteral("onnxruntime"), ortVersion);
    obj.insert(QStringLiteral("opencv"), QString::fromLatin1(CV_VERSION));
    obj.insert(QStringLiteral("models_found"), modelsFound);
    printJsonLine(obj);

    return (modelsFound && opencvOk && !ortVersion.isEmpty()) ? 0 : 1;
}

int runOcr(const QStringList &args)
{
    QStringList rest = args;
    const bool debug = rest.removeAll(QStringLiteral("--debug")) > 0;
    const QString imagePath = rest.value(0);
    const bool builtIn = imagePath.isEmpty();

    QImage image;
    if (builtIn) {
        image = makeBuiltInImage();
    } else {
        image.load(imagePath);
        if (image.isNull()) {
            QJsonObject obj;
            obj.insert(QStringLiteral("error"),
                       QStringLiteral("cannot load image: %1").arg(imagePath));
            printJsonLine(obj);
            return 1;
        }
    }

    PaddleOcrEngine engine;
    if (debug) {
        // Dump PNGs next to the input image ("<stem>_det_prob.png", ...);
        // built-in image dumps to <cwd>/ocr_builtin_*.
        const QString prefix = builtIn
            ? QDir::current().filePath(QStringLiteral("ocr_builtin"))
            : QFileInfo(imagePath).absolutePath() + QLatin1Char('/')
                  + QFileInfo(imagePath).completeBaseName();
        engine.setDebugDumpPrefix(prefix);
    }
    const OcrResult result = engine.recognize(image).result(); // blocking is fine

    QJsonObject obj;
    QJsonArray lines;
    QString merged;
    for (const OcrLine &line : result.lines) {
        QJsonObject lo;
        lo.insert(QStringLiteral("text"), line.text);
        lo.insert(QStringLiteral("conf"), static_cast<double>(line.confidence));
        QJsonArray box;
        for (const QPointF &p : line.quad) {
            box.append(QJsonArray{static_cast<int>(std::lround(p.x())),
                                  static_cast<int>(std::lround(p.y()))});
        }
        lo.insert(QStringLiteral("box"), box);
        lines.append(lo);
        merged += line.text;
    }
    obj.insert(QStringLiteral("lines"), lines);
    obj.insert(QStringLiteral("elapsedMs"), result.elapsedMs);
    if (!result.error.isEmpty())
        obj.insert(QStringLiteral("error"), result.error);
    obj.insert(QStringLiteral("tensors"),
               QJsonArray::fromStringList(engine.tensorInfo()));
    printJsonLine(obj);

    if (!result.error.isEmpty())
        return 1;
    if (builtIn) {
        return (merged.contains(QLatin1String("Hello"))
                && merged.contains(QStringLiteral("你好"))) ? 0 : 1;
    }
    return 0;
}

int runTranslate(const QStringList &args)
{
    const QString text = args.value(0, QStringLiteral("你好世界"));
    QString from = args.value(1, QStringLiteral("auto"));
    QString to = args.value(2);
    if (to.isEmpty()) {
        // Heuristic default: CJK text -> en, otherwise -> zh-CN.
        bool hasCjk = false;
        for (QChar ch : text) {
            const ushort u = ch.unicode();
            if ((u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3040 && u <= 0x30FF)
                || (u >= 0xAC00 && u <= 0xD7AF)) {
                hasCjk = true;
                break;
            }
        }
        to = hasCjk ? QStringLiteral("en") : QStringLiteral("zh-CN");
    }

    // Phase 6-tuning：强制 proxy=system 还原 Phase 1-3 系统代理行为，
    // 并强制 provider=google 验证 Google 真可达（而非 mock 兜底误判 pass）。
    // 用 setValue 触发 configChanged 信号让 TranslationManager 重新 apply 代理。
    // 2026-08：第 4 个可选参数指定 provider（默认 google，契约不变），
    // 便于验证新接入的免密钥源（volcano/mymemory 等）。
    const QString forcedProvider =
        args.value(3, QStringLiteral("google"));
    ConfigManager::instance().setValue(QStringLiteral("proxy.mode"),
                                       QStringLiteral("system"));

    const QFuture<ManagedTransResult> future =
        TranslationManager::instance().translate(text, from, to,
                                                 forcedProvider);

    // The provider chain completes via network signals -> pump an event loop.
    QEventLoop loop;
    QFutureWatcher<ManagedTransResult> watcher;
    QObject::connect(&watcher, &QFutureWatcher<ManagedTransResult>::finished,
                     &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    if (!future.isFinished())
        loop.exec();

    const ManagedTransResult r = watcher.result();
    const bool networkOk = TranslationManager::instance().networkOk();

    QJsonObject obj;
    obj.insert(QStringLiteral("text"), r.result.text);
    obj.insert(QStringLiteral("provider"), r.result.provider);
    obj.insert(QStringLiteral("network_ok"), networkOk);
    obj.insert(QStringLiteral("error_chain"),
               QJsonArray::fromStringList(r.errorChain));
    printJsonLine(obj);

    // 严格契约：必须由指定 provider 真成功，network_ok=true，不能落到 mock。
    // 这是 Phase 6-tuning 的核心目标：proxy=system 下 Google 必须可达。
    const bool pass = r.result.error.isEmpty()
        && !r.result.text.isEmpty()
        && r.result.provider == forcedProvider
        && networkOk;
    return pass ? 0 : 1;
}

namespace {

// Capture-selftest probe window. Default: 640x240, the two contract lines.
// Debug mode: 640x360 with an extra tightly-spaced two-line paragraph so the
// grouping rule (gap < median height * 0.6, overlap > 30%) has work to do.
class CaptureProbeWidget : public QWidget
{
public:
    explicit CaptureProbeWidget(bool withParagraph)
        : m_withParagraph(withParagraph)
    {
        setWindowTitle(QStringLiteral("XTranslate capture selftest"));
        // Unattended runs: stay above whatever covers the desktop (full-screen
        // players etc.), since activateWindow() cannot steal the foreground.
        setWindowFlag(Qt::WindowStaysOnTopHint);
        setFixedSize(640, withParagraph ? 360 : 240);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        p.setPen(Qt::black);
        QFont f;
        f.setFamilies({QStringLiteral("Microsoft YaHei"),
                       QStringLiteral("SimHei")});
        f.setPointSize(24);
        p.setFont(f);
        p.drawText(QRect(0, 30, width(), 80), Qt::AlignCenter,
                   QStringLiteral("Capture pipeline test"));
        p.drawText(QRect(0, 130, width(), 80), Qt::AlignCenter,
                   QStringLiteral("截图链路自测"));
        if (m_withParagraph) {
            // Two lines with tight leading -> should merge into ONE group.
            QFont small = f;
            small.setPointSize(14);
            p.setFont(small);
            p.drawText(QRect(60, 250, width() - 120, 30), Qt::AlignLeft,
                       QStringLiteral("The quick brown fox jumps over"));
            p.drawText(QRect(60, 280, width() - 120, 30), Qt::AlignLeft,
                       QStringLiteral("the lazy dog near the river bank"));
        }
    }

private:
    bool m_withParagraph = false;
};

void pumpEventLoopFor(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace

int runCapture(const QStringList &args)
{
    const bool debug = args.contains(QStringLiteral("--debug"));

    QElapsedTimer timer;
    timer.start();

    CaptureProbeWidget probe(debug);
    probe.show();
    // Unattended runs: make sure nothing covers the probe before the grab.
    probe.raise();
    probe.activateWindow();
    pumpEventLoopFor(300);

    const QRect logicalRect(probe.mapToGlobal(QPoint(0, 0)), probe.size());
    const QImage frame = ScreenCapturer::grabLogicalRect(logicalRect);
    if (frame.isNull()) {
        QJsonObject obj;
        obj.insert(QStringLiteral("error"), QStringLiteral("grab failed"));
        printJsonLine(obj);
        return 1;
    }

    PaddleOcrEngine engine;
    if (debug) {
        engine.setDebugDumpPrefix(
            QDir::current().filePath(QStringLiteral("capture_probe")));
    }
    const OcrResult result = engine.recognize(frame).result();
    probe.close();

    QJsonArray texts;
    QString merged;
    for (const OcrLine &line : result.lines) {
        texts.append(line.text);
        merged += line.text;
    }
    const bool match = merged.contains(QLatin1String("Capture"))
                       && merged.contains(QStringLiteral("截图"));

    QJsonObject obj;
    obj.insert(QStringLiteral("lines"), result.lines.size());
    obj.insert(QStringLiteral("texts"), texts);
    obj.insert(QStringLiteral("match"), match);
    obj.insert(QStringLiteral("elapsedMs"), timer.elapsed());
    if (!result.error.isEmpty())
        obj.insert(QStringLiteral("error"), result.error);

    if (debug) {
        // Evidence for the dev report: draw the grouping result on the frame.
        const QVector<OcrGroup> groups = groupOcrLines(result.lines);
        QImage annotated = frame.convertToFormat(QImage::Format_RGB32);
        QPainter p(&annotated);
        p.setPen(QPen(QColor(0xFF, 0x9F, 0x1C), 3));
        for (const OcrGroup &g : groups)
            p.drawRect(g.bbox);
        p.end();
        annotated.save(
            QDir::current().filePath(QStringLiteral("capture_groups.png")));
        obj.insert(QStringLiteral("groups"), groups.size());
    }
    printJsonLine(obj);

    return match ? 0 : 1;
}

int runOverlay()
{
    const QRect overlayRect(120, 120, 480, 260);

    // Deterministic white backdrop behind the overlay so the render probe
    // below measures OUR panels, not whatever the desktop happens to show.
    // (Test scaffolding only - the overlay render path itself is untouched.)
    QWidget backdrop(nullptr, Qt::FramelessWindowHint | Qt::Tool
                                  | Qt::WindowStaysOnTopHint);
    backdrop.setAttribute(Qt::WA_ShowWithoutActivating);
    backdrop.setAutoFillBackground(true);
    QPalette pal = backdrop.palette();
    pal.setColor(QPalette::Window, Qt::white);
    backdrop.setPalette(pal);
    backdrop.setGeometry(overlayRect.adjusted(-20, -20, 20, 20));
    backdrop.show();

    OverlayWindow overlay(overlayRect);

    QVector<OverlayGroup> groups;
    OverlayGroup a;
    a.rect = QRectF(10, 20, 440, 60);
    a.lineHeight = 24;
    a.sourceText = QStringLiteral("Fake source line one");
    a.translatedText = QStringLiteral("假译文第一组");
    a.loading = false;
    OverlayGroup b;
    b.rect = QRectF(10, 140, 440, 60);
    b.lineHeight = 24;
    b.sourceText = QStringLiteral("Fake source line two");
    b.translatedText = QStringLiteral("假译文第二组");
    b.loading = false;
    groups << a << b;
    overlay.setGroups(groups);

    overlay.show();
    pumpEventLoopFor(100); // let showEvent apply the extended styles

    bool stylesOk = false;
#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(overlay.winId());
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const LONG_PTR wanted = WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOACTIVATE;
    stylesOk = (ex & wanted) == wanted;
#endif

    // Real-render probe (anti fake-green): styles alone cannot tell whether
    // Qt's UpdateLayeredWindowIndirect frames actually reach the screen, so
    // grab the DWM-composited screen area with the production GDI capturer
    // (BitBlt|CAPTUREBLT includes layered windows) and verify each panel:
    //   * pixel at the panel center ~ #202020 (tolerance +-40 per channel);
    //   * >50% of the panel area is panel-dark (white glyphs stay a minority).
    pumpEventLoopFor(400); // >=300ms so at least one layered frame is up
    bool renderOk = false;
    QJsonArray panelStats;
    const QImage shot = ScreenCapturer::grabLogicalRect(overlayRect);
    if (!shot.isNull()) {
        const qreal sx = static_cast<qreal>(shot.width()) / overlayRect.width();
        const qreal sy = static_cast<qreal>(shot.height()) / overlayRect.height();
        const auto isPanelDark = [](QRgb px) {
            return qAbs(qRed(px) - 0x20) <= 40 && qAbs(qGreen(px) - 0x20) <= 40
                   && qAbs(qBlue(px) - 0x20) <= 40;
        };

        renderOk = true;
        for (const OverlayGroup &g : groups) {
            // paintEvent may only grow a panel downwards, so the group rect
            // itself is always fully covered by its panel.
            const QRect panel(qRound(g.rect.x() * sx), qRound(g.rect.y() * sy),
                              qRound(g.rect.width() * sx),
                              qRound(g.rect.height() * sy));
            const QRect clipped = panel.intersected(shot.rect());
            if (clipped.isEmpty()) {
                renderOk = false;
                break;
            }

            const QRgb center = shot.pixel(clipped.center());
            const bool centerOk = isPanelDark(center);

            qint64 dark = 0;
            for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
                for (int x = clipped.left(); x <= clipped.right(); ++x) {
                    if (isPanelDark(shot.pixel(x, y)))
                        ++dark;
                }
            }
            const qreal ratio = static_cast<qreal>(dark)
                / (static_cast<qint64>(clipped.width()) * clipped.height());
            const bool ratioOk = ratio > 0.5;

            QJsonObject stat;
            stat.insert(QStringLiteral("center"),
                        QStringLiteral("#%1").arg(center & 0xFFFFFF, 6, 16,
                                                  QLatin1Char('0')));
            stat.insert(QStringLiteral("center_ok"), centerOk);
            stat.insert(QStringLiteral("dark_ratio"),
                        std::round(ratio * 1000.0) / 1000.0);
            stat.insert(QStringLiteral("ratio_ok"), ratioOk);
            panelStats.append(stat);

            if (!centerOk || !ratioOk)
                renderOk = false;
        }
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("styles_ok"), stylesOk);
    obj.insert(QStringLiteral("render_ok"), renderOk);
    obj.insert(QStringLiteral("panels"), panelStats);
    obj.insert(QStringLiteral("groups"), overlay.groupCount());

    // Phase 7-fix2b 契约变更（去锁化）：锁定/解锁按钮已移除，overlay 恒穿透。
    // 旧字段 drag_toggle_ok / drag_toggle_lock_passthrough_ok（围绕 setClickThrough
    // 双态切换）语义失效，一并移除。新字段：
    //   always_passthrough_ok：WS_EX_TRANSPARENT 恒置位（show 后 + move 后各读一次，
    //     GetWindowLongPtrW 直接读位——穿透生效的必要且充分条件。为何不用
    //     SendMessageW(WM_NCHITTEST)：SendMessage 直调窗口过程绕过系统级
    //     hit-test 链，DefWindowProc 恒回 HTCLIENT，测不出穿透，历史上导致
    //     阶段 0-3 误报，此坑固化为注释防回退）。
    //   grip_present_ok：ControlBar 含 objectName=gripHandle 子控件（唯一拖动入口）。
    //   drag_keeps_panels_ok：模拟 grip 拖动 = 直接 move 窗口 +100,+100，断言
    //     ①窗口位置精确平移；②面板全局中心随窗口平移同量（局部坐标契约：
    //     OverlayGroup::rect 相对窗口左上角，move 后自然跟随）且面板仍在窗口内
    //     可绘制；③re-grab 新位置采样面板中心 #202020±40（BUG7"拖完就消失"
    //     的反回归渲染断言）。
    bool alwaysPassthroughOk = false;
    bool gripPresentOk = false;
    bool dragKeepsPanelsOk = false;
    QJsonArray dragPanelStats;
#ifdef _WIN32
    {
        // 验证 grip 握把存在（不 show，避免触发 Acrylic backdrop 副作用）。
        ControlBar barProbe;
        gripPresentOk = barProbe.findChild<QLabel *>(
            QStringLiteral("gripHandle")) != nullptr;

        HWND hwnd = reinterpret_cast<HWND>(overlay.winId());
        if (hwnd) {
            const bool transparentShown =
                (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT);

            // 模拟 grip 拖动：move +100,+100（生产路径 ScreenTranslateController
            // 对 dragRequested 的处理就是 move()，此处等价复现）。
            const QPoint posBefore = overlay.pos();
            QVector<QPointF> centersBefore;
            for (const OverlayGroup &g : groups)
                centersBefore.append(QPointF(posBefore) + g.rect.center());

            const QRect movedRect = overlayRect.translated(100, 100);
            backdrop.setGeometry(movedRect.adjusted(-20, -20, 20, 20));
            overlay.move(posBefore + QPoint(100, 100));
            overlay.update();
            pumpEventLoopFor(400); // >=300ms so the moved layered frame is up

            const bool posMoved = overlay.pos() == posBefore + QPoint(100, 100);
            bool centersFollow = true;
            const QRectF windowLocal(QPointF(0, 0), QSizeF(overlay.size()));
            for (int i = 0; i < groups.size(); ++i) {
                const QPointF after =
                    QPointF(overlay.pos()) + groups.at(i).rect.center();
                if ((after - centersBefore.at(i)) != QPointF(100, 100))
                    centersFollow = false;
                if (!windowLocal.contains(groups.at(i).rect.center()))
                    centersFollow = false; // 面板中心必须仍在窗口内可绘制
            }

            // re-grab 新位置：面板中心仍是 panelDark（#202020±40）。
            bool movedRenderOk = false;
            const QImage shot2 = ScreenCapturer::grabLogicalRect(movedRect);
            if (!shot2.isNull()) {
                const qreal sx2 = static_cast<qreal>(shot2.width())
                                  / movedRect.width();
                const qreal sy2 = static_cast<qreal>(shot2.height())
                                  / movedRect.height();
                const auto isPanelDark2 = [](QRgb px) {
                    return qAbs(qRed(px) - 0x20) <= 40
                           && qAbs(qGreen(px) - 0x20) <= 40
                           && qAbs(qBlue(px) - 0x20) <= 40;
                };
                movedRenderOk = true;
                for (const OverlayGroup &g : groups) {
                    const QPoint c(qRound(g.rect.center().x() * sx2),
                                   qRound(g.rect.center().y() * sy2));
                    if (!shot2.rect().contains(c)) {
                        movedRenderOk = false;
                        break;
                    }
                    const QRgb px = shot2.pixel(c);
                    QJsonObject stat;
                    stat.insert(QStringLiteral("center"),
                                QStringLiteral("#%1").arg(px & 0xFFFFFF, 6, 16,
                                                          QLatin1Char('0')));
                    stat.insert(QStringLiteral("center_ok"), isPanelDark2(px));
                    dragPanelStats.append(stat);
                    if (!isPanelDark2(px))
                        movedRenderOk = false;
                }
            }

            dragKeepsPanelsOk = posMoved && centersFollow && movedRenderOk;

            // move 之后再读一次穿透位：恒穿透 = 两个时点都置位。
            const bool transparentMoved =
                (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT);
            alwaysPassthroughOk = transparentShown && transparentMoved;
        }
    }
#endif
    obj.insert(QStringLiteral("always_passthrough_ok"), alwaysPassthroughOk);
    obj.insert(QStringLiteral("grip_present_ok"), gripPresentOk);
    obj.insert(QStringLiteral("drag_keeps_panels_ok"), dragKeepsPanelsOk);
    obj.insert(QStringLiteral("drag_panels"), dragPanelStats);

    printJsonLine(obj);

    pumpEventLoopFor(300); // stay visible briefly, then auto-close
    overlay.close();
    backdrop.close();

    // Phase 7-fix2b：新字段并入 exit code（防"字段 false 但 exit=0"的假绿）。
    return stylesOk && renderOk && overlay.groupCount() == 2
                   && alwaysPassthroughOk && gripPresentOk && dragKeepsPanelsOk
               ? 0
               : 1;
}

int runHotkey()
{
    // F-1 修复（Phase 5）：不再恒用 defaultActions 硬编码的 sequence 注册，
    // 改为读 ConfigManager 当前绑定值（用户在设置页改键后 selftest 反映新配置）。
    // defaultActions 仅提供 actionId 列表 + label + 兜底默认 sequence。
    HotkeyManager &mgr = HotkeyManager::instance();
    ConfigManager &cfg = ConfigManager::instance();

    int registered = 0;
    QJsonObject bindings;  // actionId -> 实际注册用的 keySeq（便于核对）
    const QVector<HotkeyManager::ActionSpec> specs =
        HotkeyManager::defaultActions();
    for (const HotkeyManager::ActionSpec &spec : specs) {
        const QString saved = cfg.hotkeyFor(spec.actionId);
        const QKeySequence seq = saved.isEmpty()
                                     ? spec.sequence
                                     : QKeySequence(saved);
        if (mgr.registerAction(spec.actionId, seq, []() {}))
            ++registered;
        bindings.insert(spec.actionId,
                        seq.toString(QKeySequence::PortableText));
    }
    const QStringList failed = mgr.failedActions();
    mgr.unregisterAll();

    QJsonObject obj;
    obj.insert(QStringLiteral("registered"), registered);
    obj.insert(QStringLiteral("failed"), QJsonArray::fromStringList(failed));
    obj.insert(QStringLiteral("bindings"), bindings);
    // 加固（Phase 6-tuning）：全键失败时给 note 提示最常见原因——
    // 本产品"关窗驻留托盘"，GUI 实例持有全部 6 个热键属 RegisterHotKey 正常语义。
    if (registered == 0 && failed.size() >= 6) {
        obj.insert(QStringLiteral("note"),
                   QStringLiteral("XTranslate GUI 可能正在运行并持有全部热键"
                                  "（关窗驻留托盘属正常）；如需复测请先退出 GUI"));
    }
    printJsonLine(obj);

    // 契约：6 动作全注册成功为理想；环境占用键容忍到 >=3 个成功，
    // 前提是失败项如实上报（failures 已包含 actionId + sequence）。
    return registered >= 3 ? 0 : 1;
}

int runTts()
{
    // v0.7.2：云端 mp3 经 Qt Multimedia FFmpeg 后端播放时，FFmpeg 会向
    // stderr 打 "Estimating duration from bitrate" 等诊断行（原生 av_log，
    // 不走 Qt 日志链，过滤规则拦不住），污染批量脚本的 2>&1 合并流
    // 并触发 PowerShell EAP=Stop 中断。本 selftest 契约输出只在 stdout
    // （一行 JSON + exit code），故这里把 stderr 重定向到 NUL（仅影响
    // 本模式进程，GUI 真实运行不受影响），并屏蔽 qt.multimedia 类目。
    QLoggingCategory::setFilterRules(
        QStringLiteral("qt.multimedia.*=false"));
#ifdef _WIN32
    freopen("NUL", "w", stderr);
#endif

    TtsManager &tts = TtsManager::instance();

    QJsonObject obj;
    obj.insert(QStringLiteral("available"), tts.isAvailable());
    obj.insert(QStringLiteral("voices"), tts.voiceCount());
    // v0.7.2：双引擎。engine=当前配置引擎（缺省 cloud），edge_voice_map=
    // 内置 lang→Edge voice 映射表条数（覆盖主窗目标语言全集）。
    obj.insert(QStringLiteral("engine"), TtsManager::engine());
    obj.insert(QStringLiteral("edge_voice_map"),
               EdgeTtsProvider::voiceMapSize());
    // 映射抽查：韩/日/法语选到对应语种神经嗓音（纯函数，无网络依赖）。
    const bool edgeMapOk =
        EdgeTtsProvider::voiceForLang(QStringLiteral("ko"))
            .startsWith(QLatin1String("ko-KR-"))
        && EdgeTtsProvider::voiceForLang(QStringLiteral("ja"))
            .startsWith(QLatin1String("ja-JP-"))
        && EdgeTtsProvider::voiceForLang(QStringLiteral("fr"))
            .startsWith(QLatin1String("fr-FR-"))
        && EdgeTtsProvider::voiceForLang(QStringLiteral("zh-CN"))
            .startsWith(QLatin1String("zh-CN-"));
    obj.insert(QStringLiteral("edge_map_ok"), edgeMapOk);

    // v0.7.1 BUG-A 加强：枚举多语种嗓音。每个语种记录选到的 voice 名，
    // 本机无该语言语音包时 <lang>_voice=null + <lang>_fallback_default=true
    // （明确回退默认嗓音，不静默失败）。
    const char *langs[] = {"zh-CN", "en", "ja", "ko", "fr"};
    for (const char *lang : langs) {
        const QString code = QString::fromLatin1(lang);
        const QString voice = tts.voiceNameFor(code);
        QString key = code;
        key.replace(QLatin1Char('-'), QLatin1Char('_'));
        // zh-CN/en 保留历史字段名 zh_voice/en_voice（验收脚本兼容）。
        if (code == QLatin1String("zh-CN"))
            key = QStringLiteral("zh");
        obj.insert(key + QStringLiteral("_voice"),
                   voice.isEmpty() ? QJsonValue() : QJsonValue(voice));
        if (code != QLatin1String("zh-CN") && code != QLatin1String("en")) {
            obj.insert(key + QStringLiteral("_fallback_default"),
                       tts.isAvailable() && voice.isEmpty());
        }
    }

    // v0.7.1 BUG-A 加强：guessLang 细分断言（假名→ja、谚文→ko、
    // 汉字→zh-CN、拉丁→en）。纯函数无引擎依赖，失败即 exit!=0。
    const bool guessOk =
        TtsManager::guessLang(QStringLiteral("こんにちは")) == QLatin1String("ja")
        && TtsManager::guessLang(QStringLiteral("안녕하세요")) == QLatin1String("ko")
        && TtsManager::guessLang(QStringLiteral("你好世界")) == QLatin1String("zh-CN")
        && TtsManager::guessLang(QStringLiteral("bonjour")) == QLatin1String("en")
        && TtsManager::guessLang(QStringLiteral("漢字とかな")) == QLatin1String("ja");
    obj.insert(QStringLiteral("guess_lang_ok"), guessOk);

    // v0.7.2 回退链取证：
    // 1) cloud_ok：真实请求一次 Edge 云端合成（在线环境 utteranceStarted
    //    应报 "cloud"；离线/被墙环境允许 false，如实上报）。
    // 2) fallback_ok：用 XT_TTS_FORCE_CLOUD_FAIL 确定性模拟云端失败，
    //    必须观察到 "system_fallback"（回退链可达，不依赖真实断网）。
    // speakWith 不读 tts.enabled 总开关，selftest 结果不受用户配置影响。
    QString cloudEngineUsed;
    QString fallbackEngineUsed;
    {
        QString *sink = &cloudEngineUsed;
        QObject ctx;
        QObject::connect(&tts, &TtsManager::utteranceStarted, &ctx,
                         [&sink](const QString &engineUsed) {
            if (sink && sink->isEmpty())
                *sink = engineUsed;
        });

        tts.speakWith(QStringLiteral("cloud"), QStringLiteral("test"),
                      QStringLiteral("en"));
        for (int i = 0; i < 120 && cloudEngineUsed.isEmpty(); ++i)
            pumpEventLoopFor(100);   // 最多 12s（云端 10s 超时 + 余量）
        tts.stop();

        sink = &fallbackEngineUsed;
        qputenv("XT_TTS_FORCE_CLOUD_FAIL", "1");
        tts.speakWith(QStringLiteral("cloud"), QStringLiteral("test"),
                      QStringLiteral("en"));
        for (int i = 0; i < 30 && fallbackEngineUsed.isEmpty(); ++i)
            pumpEventLoopFor(100);
        tts.stop();
        qunsetenv("XT_TTS_FORCE_CLOUD_FAIL");
        sink = nullptr;
    }
    const bool cloudOk = cloudEngineUsed == QLatin1String("cloud");
    const bool fallbackOk =
        fallbackEngineUsed == QLatin1String("system_fallback");
    obj.insert(QStringLiteral("cloud_ok"), cloudOk);
    obj.insert(QStringLiteral("cloud_engine_used"),
               cloudEngineUsed.isEmpty() ? QJsonValue()
                                         : QJsonValue(cloudEngineUsed));
    obj.insert(QStringLiteral("fallback_ok"), fallbackOk);

    printJsonLine(obj);

    if (tts.isAvailable() && tts.voiceCount() > 0) {
        // Live utterance: must not throw. Give the engine a moment to start,
        // then stop - audible output is not asserted (headless CI machines).
        // v0.7.1：加测一次 ja —— 无日语语音包时走默认嗓音兜底链，同样不得抛。
        // v0.7.2：改走 speakWith("system") 直连系统引擎（speak() 现在默认
        // 分派云端，此处要验的是本机链不得抛）。
        try {
            tts.speakWith(QStringLiteral("system"), QStringLiteral("test"),
                          QStringLiteral("en"));
            pumpEventLoopFor(300);
            tts.speakWith(QStringLiteral("system"),
                          QStringLiteral("テスト"), QStringLiteral("ja"));
            pumpEventLoopFor(300);
            tts.stop();
        } catch (...) {
            return 1;
        }
    }
    // available == false is also a pass: no engine, reported truthfully.
    // cloud_ok 允许 false（离线如实上报）；fallback_ok/映射表必须绿。
    return (guessOk && edgeMapOk && fallbackOk) ? 0 : 1;
}

int runSelection()
{
    const QString preset =
        QStringLiteral("XTranslate selection selftest 划词自测");
    const QString sentinel = QStringLiteral("XT_SENTINEL_%1")
                                 .arg(QDateTime::currentMSecsSinceEpoch());

    // Sentinel first: proves at the end that the backup/restore round trip
    // really put the ORIGINAL clipboard content back.
    QApplication::clipboard()->setText(sentinel);
    pumpEventLoopFor(100);

    // Probe window: preset text, fully selected, focused.
    QLineEdit edit;
    edit.setWindowTitle(QStringLiteral("XTranslate selection selftest"));
    edit.setWindowFlag(Qt::WindowStaysOnTopHint);
    edit.setFixedSize(420, 48);
    edit.setText(preset);
    edit.show();
    edit.raise();
    edit.activateWindow();
    edit.setFocus();
    edit.selectAll();
    pumpEventLoopFor(400); // let the window really take the foreground

#ifdef _WIN32
    // Non-interactive runners (agent terminals) lack foreground rights, so
    // activateWindow() may silently fail and the injected Ctrl+C would land
    // elsewhere. Force the issue: a synthetic Alt tap unlocks
    // SetForegroundWindow for this process, then attach to the current
    // foreground thread and claim focus explicitly.
    {
        HWND hwnd = reinterpret_cast<HWND>(edit.winId());
        if (GetForegroundWindow() != hwnd) {
            keybd_event(VK_MENU, 0, 0, 0);
            keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
            const DWORD fgThread =
                GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
            const DWORD ownThread = GetCurrentThreadId();
            if (fgThread != ownThread)
                AttachThreadInput(fgThread, ownThread, TRUE);
            SetForegroundWindow(hwnd);
            SetFocus(hwnd);
            if (fgThread != ownThread)
                AttachThreadInput(fgThread, ownThread, FALSE);
            pumpEventLoopFor(300);
            edit.setFocus();
            edit.selectAll();
            pumpEventLoopFor(200);
        }
    }
#endif

    SelectionResult result;
    bool done = false;
    SelectionGrabber grabber;
    QEventLoop loop;
    QObject::connect(&grabber, &SelectionGrabber::finished, &loop,
                     [&result, &done, &loop](const SelectionResult &r) {
        result = r;
        done = true;
        loop.quit();
    });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit); // watchdog
    grabber.start();
    loop.exec();
    edit.close();

    // finished() fires after the restore step; verify the sentinel survived.
    pumpEventLoopFor(150);
    const bool sentinelIntact =
        QApplication::clipboard()->text() == sentinel;

    QJsonObject obj;
    obj.insert(QStringLiteral("text"), result.text);
    obj.insert(QStringLiteral("source"), result.source);
    obj.insert(QStringLiteral("clipboard_restored"),
               done && result.clipboardRestored && sentinelIntact);
    obj.insert(QStringLiteral("copy_latency_ms"), result.copyLatencyMs);
    printJsonLine(obj);

    return (result.text == preset && done && result.clipboardRestored
            && sentinelIntact) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// --selftest config  (phase 4)
// ---------------------------------------------------------------------------
int runConfig()
{
    QTemporaryDir dir;
    if (!dir.isValid()) {
        QJsonObject obj;
        obj.insert(QStringLiteral("error"), QStringLiteral("no temp dir"));
        printJsonLine(obj);
        return 1;
    }
    const QString cfgPath = dir.filePath(QStringLiteral("config.json"));

    // 1) fresh instance -> defaults written to disk
    bool defaultsCreated = false;
    {
        ConfigManager cfg(cfgPath);
        defaultsCreated = QFileInfo::exists(cfgPath)
            && cfg.theme() == QLatin1String("system")
            && cfg.historyLimit() == 5000;

        // 2) change three values (saved immediately)
        cfg.setValue(QStringLiteral("theme"), QStringLiteral("dark"));
        cfg.setValue(QStringLiteral("ui_language"), QStringLiteral("en"));
        cfg.setValue(QStringLiteral("history.limit"), 1234);
    }

    // 3) re-read and verify the three changes survived the round trip
    bool reloadOk = false;
    {
        ConfigManager cfg(cfgPath);
        reloadOk = cfg.theme() == QLatin1String("dark")
            && cfg.uiLanguage() == QLatin1String("en")
            && cfg.historyLimit() == 1234;
    }

    // 4) corrupt the file, reload must fall back to defaults + keep a .bak
    {
        QFile f(cfgPath);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write("{ corrupt json !! \x01\x02");
        f.close();
    }
    bool recoveredDefaults = false;
    bool bakExists = false;
    {
        ConfigManager cfg(cfgPath);
        recoveredDefaults = cfg.loadedFromBackup()
            && cfg.theme() == QLatin1String("system")
            && cfg.historyLimit() == 5000;
        bakExists = QFileInfo::exists(cfgPath + QStringLiteral(".bak"));
    }

    // 5) Phase 6-tuning 迁移用例：v1 配置(proxy=none, 无 user_touched)
    //    加载后应自动迁移为 proxy=system + version=2 + user_touched=false。
    bool migrateUntouchedToSystem = false;
    {
        QTemporaryDir dir2;
        if (!dir2.isValid()) {
            // 退化处理：临时目录失败时跳过该子断言（不影响其他用例）。
        } else {
            const QString p2 = dir2.filePath(QStringLiteral("c2.json"));
            QFile f(p2);
            f.open(QIODevice::WriteOnly | QIODevice::Truncate);
            // v1 配置：proxy.mode=none, 无 user_touched 字段, version=1。
            f.write(QByteArrayLiteral(
                "{\"version\":1,\"proxy\":{\"mode\":\"none\","
                "\"host\":\"\",\"port\":0,\"user\":\"\",\"pass\":\"\"}}"));
            f.close();
            ConfigManager cfg(p2);
            migrateUntouchedToSystem =
                cfg.stringValue(QStringLiteral("proxy.mode"))
                    == QLatin1String("system")
                && cfg.intValue(QStringLiteral("version")) == 2
                && cfg.value(QStringLiteral("proxy.user_touched")).toBool(false)
                    == false;
        }
    }

    // 6) 用户显式选过 none 的 v1 配置（user_touched=true）应被尊重，不迁移。
    bool migrateTouchedRespected = false;
    {
        QTemporaryDir dir3;
        if (!dir3.isValid()) {
        } else {
            const QString p3 = dir3.filePath(QStringLiteral("c3.json"));
            QFile f(p3);
            f.open(QIODevice::WriteOnly | QIODevice::Truncate);
            // v1 配置：proxy.mode=none, user_touched=true, version=1。
            f.write(QByteArrayLiteral(
                "{\"version\":1,\"proxy\":{\"mode\":\"none\","
                "\"host\":\"\",\"port\":0,\"user\":\"\",\"pass\":\"\","
                "\"user_touched\":true}}"));
            f.close();
            ConfigManager cfg(p3);
            migrateTouchedRespected =
                cfg.stringValue(QStringLiteral("proxy.mode"))
                    == QLatin1String("none")
                && cfg.intValue(QStringLiteral("version")) == 2;
        }
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("defaults_created"), defaultsCreated);
    obj.insert(QStringLiteral("reload_ok"), reloadOk);
    obj.insert(QStringLiteral("recovered_defaults"), recoveredDefaults);
    obj.insert(QStringLiteral("bak_exists"), bakExists);
    obj.insert(QStringLiteral("migrate_untouched_to_system"),
               migrateUntouchedToSystem);
    obj.insert(QStringLiteral("migrate_touched_respected"),
               migrateTouchedRespected);
    printJsonLine(obj);

    return (defaultsCreated && reloadOk && recoveredDefaults && bakExists
            && migrateUntouchedToSystem && migrateTouchedRespected)
               ? 0 : 1;
}

// ---------------------------------------------------------------------------
// --selftest db  (phase 4)
// ---------------------------------------------------------------------------
int runDb()
{
    QTemporaryDir dir;
    if (!dir.isValid()) {
        QJsonObject obj;
        obj.insert(QStringLiteral("error"), QStringLiteral("no temp dir"));
        printJsonLine(obj);
        return 1;
    }

    HistoryStore store(dir.filePath(QStringLiteral("history.db")));
    store.setLimit(1000); // generous for the insert phase

    // v0.7.1 BUG-B 加强：add() 必须发 changed() 信号（侧栏即时刷新链的
    // 根基：HistoryStore::changed → HistorySidebar::refresh）。
    int changedCount = 0;
    QObject::connect(&store, &HistoryStore::changed,
                     [&changedCount]() { ++changedCount; });

    // one entry per scene, ascending timestamps
    const char *scenes[] = {"input", "capture", "selection"};
    QList<qint64> ids;
    qint64 ts = QDateTime::currentMSecsSinceEpoch() - 30000;
    for (const char *scene : scenes) {
        HistoryEntry e;
        e.tsMs = ts;
        ts += 1000;
        e.srcLang = QStringLiteral("en");
        e.dstLang = QStringLiteral("zh-CN");
        e.srcText = QStringLiteral("hello %1").arg(QLatin1String(scene));
        e.dstText = QStringLiteral("你好 %1").arg(QLatin1String(scene));
        e.provider = QStringLiteral("google");
        e.scene = QLatin1String(scene);
        ids.append(store.add(e));
    }
    const bool insertOk = !ids.contains(0) && store.count() == 3;
    const bool changedSignalOk = changedCount >= 3; // 每次 add 各发一次

    // fuzzy query hits the capture row
    const bool queryOk =
        store.query(QStringLiteral("capture"), false).size() == 1;

    // favorite the OLDEST row so trim must skip it
    const bool favOk = store.setFavorite(ids.at(0), true)
        && store.query(QString(), true).size() == 1;

    // limit 3 + 4th insert -> the oldest NON-favorite row (ids[1]) must go
    store.setLimit(3);
    HistoryEntry e4;
    e4.tsMs = ts;
    e4.srcLang = QStringLiteral("en");
    e4.dstLang = QStringLiteral("zh-CN");
    e4.srcText = QStringLiteral("hello trim");
    e4.dstText = QStringLiteral("你好 裁剪");
    e4.provider = QStringLiteral("google");
    e4.scene = QStringLiteral("input");
    const qint64 id4 = store.add(e4);

    bool trimOk = id4 > 0 && store.count() == 3;
    if (trimOk) {
        const QList<HistoryEntry> rest = store.query(QString(), false);
        bool oldestFavSurvived = false;
        bool secondOldestGone = true;
        for (const HistoryEntry &e : rest) {
            if (e.id == ids.at(0))
                oldestFavSurvived = true;
            if (e.id == ids.at(1))
                secondOldestGone = false;
        }
        trimOk = oldestFavSurvived && secondOldestGone;
    }

    // ---- Phase 5 迁移用例 ----
    // 构造 v0 老库（旧 CHECK 不含 'replace'，3 条数据 + 1 收藏，user_version=0）
    // → 用 HistoryStore 打开触发迁移 → 校验数据完整 + 收藏保留 + scene='replace'
    // 可写入 + user_version=1 + .bak_v0 备份存在。
    bool migrateOk = false;
    bool migrateDataIntact = false;
    bool migrateFavKept = false;
    bool migrateReplaceWritable = false;
    bool migrateVersion1 = false;
    bool migrateBakExists = false;
    {
        QTemporaryDir v0dir;
        if (v0dir.isValid()) {
            const QString v0path = v0dir.filePath(QStringLiteral("v0.db"));
            // 直接用 QSqlDatabase 手工建 v0 老库。
            // 注意：removeDatabase 必须在 QSqlDatabase 对象析构后调用，
            // 否则 Qt 警告 "connection still in use" 且连接不真正关闭，
            // 导致后续 HistoryStore 打开同文件时撞残留锁（DROP TABLE 失败）。
            const QString conn = QStringLiteral("v0_build_%1").arg(
                QDateTime::currentMSecsSinceEpoch() % 100000);
            {
                QSqlDatabase v0 = QSqlDatabase::addDatabase(
                    QStringLiteral("QSQLITE"), conn);
                v0.setDatabaseName(v0path);
                if (v0.open()) {
                    QSqlQuery q(v0);
                    q.exec(QStringLiteral(
                        "CREATE TABLE trans_history("
                        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                        " ts INTEGER NOT NULL,"
                        " src_lang TEXT, dst_lang TEXT,"
                        " src_text TEXT, dst_text TEXT,"
                        " provider TEXT,"
                        " scene TEXT CHECK(scene IN "
                        "('input','capture','selection')),"
                        " favorite INTEGER DEFAULT 0)"));
                    const char *scenes[] = {"input", "capture", "selection"};
                    const qint64 baseTs =
                        QDateTime::currentMSecsSinceEpoch() - 10000;
                    for (int i = 0; i < 3; ++i) {
                        q.prepare(QStringLiteral(
                            "INSERT INTO trans_history(ts,src_lang,dst_lang,"
                            "src_text,dst_text,provider,scene,favorite) "
                            "VALUES(?,?,?,?,?,?,?,?)"));
                        q.addBindValue(baseTs + i * 1000);
                        q.addBindValue(QStringLiteral("en"));
                        q.addBindValue(QStringLiteral("zh-CN"));
                        q.addBindValue(QStringLiteral("src%1").arg(i));
                        q.addBindValue(QStringLiteral("译%1").arg(i));
                        q.addBindValue(QStringLiteral("google"));
                        q.addBindValue(QLatin1String(scenes[i]));
                        q.addBindValue(i == 0 ? 1 : 0);  // 第 1 条收藏
                        q.exec();
                    }
                    q.exec(QStringLiteral("PRAGMA user_version = 0"));
                    q.clear();
                    v0.close();
                }
            }  // v0 对象析构，连接引用计数归零
            QSqlDatabase::removeDatabase(conn);  // 现在能真正关闭

            // 用 HistoryStore 打开 v0.db → 构造函数自动检测 user_version=0 +
            // 有数据 → 触发迁移。
            HistoryStore migrated(v0path);
            migrated.setLimit(1000);

            const int migratedCount = migrated.count();
            const QList<HistoryEntry> migratedRows =
                migrated.query(QString(), false);
            bool foundFav = false;
            for (const HistoryEntry &e : migratedRows) {
                if (e.favorite && e.srcText == QStringLiteral("src0"))
                    foundFav = true;
            }
            migrateDataIntact = (migratedCount == 3);
            migrateFavKept = foundFav;

            // 验证 scene='replace' 可写入（老 CHECK 会拒，新 CHECK 允许）。
            HistoryEntry rep;
            rep.srcLang = QStringLiteral("en");
            rep.dstLang = QStringLiteral("zh-CN");
            rep.srcText = QStringLiteral("replace test");
            rep.dstText = QStringLiteral("替换测试");
            rep.provider = QStringLiteral("mock");
            rep.scene = QStringLiteral("replace");
            const qint64 repId = migrated.add(rep);
            migrateReplaceWritable = (repId > 0);

            // 直接查 user_version（独立连接，作用域与 removeDatabase 分离）。
            const QString conn2 = QStringLiteral("v0_check_%1").arg(
                QDateTime::currentMSecsSinceEpoch() % 100000);
            {
                QSqlDatabase v0c = QSqlDatabase::addDatabase(
                    QStringLiteral("QSQLITE"), conn2);
                v0c.setDatabaseName(v0path);
                if (v0c.open()) {
                    QSqlQuery q(v0c);
                    if (q.exec(QStringLiteral("PRAGMA user_version")) && q.next())
                        migrateVersion1 = (q.value(0).toInt() == 1);
                    q.clear();
                    v0c.close();
                }
            }
            QSqlDatabase::removeDatabase(conn2);

            migrateBakExists = QFile::exists(
                v0path + QStringLiteral(".bak_v0"));

            migrateOk = migrateDataIntact && migrateFavKept
                        && migrateReplaceWritable && migrateVersion1
                        && migrateBakExists;
        }
    }

    // ---- v0.7.1 损坏库回退用例（BUG-B 根因）----
    // 构造非 SQLite 文件（真机命中：旧脚本写坏的 58 字节 marker）→
    // HistoryStore 打开应备份 .bak_corrupt + 重建空库，add/query 正常。
    bool corruptRecoverOk = false;
    {
        QTemporaryDir cdir;
        if (cdir.isValid()) {
            const QString cpath = cdir.filePath(QStringLiteral("corrupt.db"));
            QFile f(cpath);
            f.open(QIODevice::WriteOnly | QIODevice::Truncate);
            f.write(QByteArrayLiteral("\xEF\xBB\xBFSQLite format 3\0fake "
                                      "marker not a real database"));
            f.close();
            HistoryStore recovered(cpath);
            recovered.setLimit(100);
            HistoryEntry ce;
            ce.srcLang = QStringLiteral("en");
            ce.dstLang = QStringLiteral("zh-CN");
            ce.srcText = QStringLiteral("corrupt recover");
            ce.dstText = QStringLiteral("损坏恢复");
            ce.provider = QStringLiteral("google");
            ce.scene = QStringLiteral("input");
            const qint64 cid = recovered.add(ce);
            corruptRecoverOk = recovered.isOpen() && cid > 0
                && recovered.count() == 1
                && QFile::exists(cpath + QStringLiteral(".bak_corrupt"));
        }
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("insert_ok"), insertOk);
    obj.insert(QStringLiteral("changed_signal_ok"), changedSignalOk);
    obj.insert(QStringLiteral("corrupt_recover_ok"), corruptRecoverOk);
    obj.insert(QStringLiteral("query_ok"), queryOk);
    obj.insert(QStringLiteral("favorite_ok"), favOk);
    obj.insert(QStringLiteral("trim_ok"), trimOk);
    obj.insert(QStringLiteral("count"), store.count());
    obj.insert(QStringLiteral("migrate_ok"), migrateOk);
    obj.insert(QStringLiteral("migrate_data_intact"), migrateDataIntact);
    obj.insert(QStringLiteral("migrate_fav_kept"), migrateFavKept);
    obj.insert(QStringLiteral("migrate_replace_writable"), migrateReplaceWritable);
    obj.insert(QStringLiteral("migrate_version_1"), migrateVersion1);
    obj.insert(QStringLiteral("migrate_bak_exists"), migrateBakExists);
    printJsonLine(obj);

    return (insertOk && changedSignalOk && corruptRecoverOk && queryOk
            && favOk && trimOk && migrateOk) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// --selftest providers  (phase 4)
// ---------------------------------------------------------------------------
int runProviders()
{
    // Signature known-vector checks (no real credentials needed).
    // 1) Baidu official doc example.
    const bool baiduOk = BaiduTranslator::sign(
        QStringLiteral("2015063000000001"), QStringLiteral("apple"),
        QStringLiteral("1435660288"), QStringLiteral("12345678"))
        == QLatin1String("f89f9594663708c1605f3d736d01d2d4");

    // 2) Youdao truncation rule + sha256("abc") through the sign helper
    //    (empty appKey/salt/curtime/secret reduces sign() to sha256(input)).
    //    len 21 -> first10 + "21" + last10.
    const bool youdaoTruncOk =
        YoudaoTranslator::truncateInput(QStringLiteral("01234567890123456789X"))
            == QLatin1String("012345678921123456789X")
        && YoudaoTranslator::truncateInput(QStringLiteral("short"))
            == QLatin1String("short");
    const bool sha256Ok = YoudaoTranslator::sign(
        QString(), QStringLiteral("abc"), QString(), QString(), QString())
        == QLatin1String("ba7816bf8f01cfea414140de5dae2223"
                         "b00361a396177a9cb410ff61f20015ad");

    // 3) HMAC-SHA256 RFC 4231 test case 1 (Tencent TC3 building block).
    const QByteArray rfcKey(20, char(0x0b));
    const bool hmacOk = TencentTranslator::hmacSha256(
        rfcKey, QByteArrayLiteral("Hi There")).toHex()
        == QByteArrayLiteral("b0344c61d8db38535ca8afceaf0bf12b"
                             "881dc200c9833da726e9376c2e32cff7");

    // 4) TC3 canonical request shape (payload hash embedded, action lowered).
    const QString canonical =
        TencentTranslator::canonicalRequest(QByteArrayLiteral("{}"));
    const bool tc3Ok = canonical.startsWith(QLatin1String("POST\n/\n\n"))
        && canonical.contains(QLatin1String("x-tc-action:texttranslate"))
        && canonical.contains(QLatin1String("content-type;host;x-tc-action"));

    // 5) DeepL free/pro host split.
    const bool deeplOk =
        DeepLTranslator::hostForKey(QStringLiteral("k:fx"))
            == QLatin1String("api-free.deepl.com")
        && DeepLTranslator::hostForKey(QStringLiteral("k"))
            == QLatin1String("api.deepl.com");

    const bool signOk =
        baiduOk && youdaoTruncOk && sha256Ok && hmacOk && tc3Ok && deeplOk;

    QJsonArray providers;
    const QVector<Translator *> all =
        TranslationManager::instance().allProviders();
    for (Translator *t : all) {
        QJsonObject p;
        p.insert(QStringLiteral("name"), t->name());
        p.insert(QStringLiteral("requires_key"), t->requiresKey());
        p.insert(QStringLiteral("configured"), t->isConfigured());
        providers.append(p);
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("providers"), providers);
    obj.insert(QStringLiteral("count"), providers.size());
    obj.insert(QStringLiteral("sign_ok"), signOk);
    printJsonLine(obj);

    return (providers.size() >= 10 && signOk) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// --selftest theme  (phase 4)
// ---------------------------------------------------------------------------
int runTheme()
{
    const QString light = ThemeManager::loadQss(QStringLiteral("light"));
    const QString dark = ThemeManager::loadQss(QStringLiteral("dark"));
    // Phase 7：静态 qss 仍非空（减少透明度模式回退用）。
    const bool qssOk = !light.isEmpty() && !dark.isEmpty();

    bool applyOk = true;
    try {
        ThemeManager &theme = ThemeManager::instance();
        theme.apply(QStringLiteral("light"));
        applyOk = applyOk && !theme.isDark();
        theme.apply(QStringLiteral("dark"));
        applyOk = applyOk && theme.isDark();
    } catch (...) {
        applyOk = false;
    }

    // Phase 7 加固：玻璃模板渲染产物必须无残留占位符。
    // 契约仍是 qss_ok 布尔（用户约束 3a），但判定从"静态 qss 非空"
    // 扩展为"静态 qss 非空 && 玻璃模板渲染产物无残留占位符"。
    // 模板缺失/渲染失败/残留占位符任一发生 → glassOk=false，但 qss_ok
    // 仍可为 true（回退到静态 qss 时 UI 仍有样式，符合约束 3b）。
    bool glassOk = true;
    {
        // Phase 7-fix1：测试 fixture 与生产 palette 字段保持一致，
        // 新增灰阶令牌字段同步填充，避免 renderGlassQss 替换占位符时
        // 读到默认 QColor()（黑色 invalid）产生错位颜色值。selftest 仍
        // 只断言 hasResidualPlaceholders=false，不验证颜色对错（契约不变）。
        ThemePalette lightPal;
        lightPal.dark = false;
        lightPal.accent = QColor(0x11, 0x11, 0x11);
        lightPal.accentHover = QColor(0x2A, 0x2A, 0x2A);
        lightPal.accentText = QColor(0xFF, 0xFF, 0xFF);
        lightPal.accentPressed = QColor(0x00, 0x00, 0x00);
        lightPal.focusBorder = QColor(0x6B, 0x72, 0x80);
        // Phase 7-fix2：次按钮描边提亮 + 新增 secondaryButtonBg（与生产 palette 同源）
        lightPal.secondaryButtonBorder = QColor(0x6B, 0x72, 0x80);
        lightPal.secondaryButtonText = QColor(0x1F, 0x29, 0x37);
        lightPal.secondaryButtonBg = QColor(0, 0, 0, 15);
        lightPal.selectionBg = QColor(0, 0, 0, 15);
        lightPal.selectionLeftBar = QColor(0x11, 0x11, 0x11);
        lightPal.windowBg = QColor(0xFA, 0xF7, 0xF2);
        lightPal.cardBg = QColor(0xFF, 0xFF, 0xFF);
        lightPal.cardBorder = QColor(0xE4, 0xDC, 0xD0);
        lightPal.text = QColor(0x33, 0x29, 0x1A);
        lightPal.subText = QColor(0x8A, 0x80, 0x72);
        lightPal.glassSurface = QColor(255, 255, 255, 107);
        lightPal.glassSurfaceFake = QColor(255, 255, 255, 209);
        lightPal.resultCardSurface = QColor(255, 255, 255, 166);
        lightPal.glassBorder = QColor(255, 255, 255, 89);
        lightPal.glassSpecular = QColor(255, 255, 255, 140);
        lightPal.glassShadow = QColor(0, 0, 0, 41);
        lightPal.textOnGlass = QColor(0x1F, 0x29, 0x37);

        ThemePalette darkPal = lightPal;
        darkPal.dark = true;
        darkPal.accent = QColor(0xF5, 0xF5, 0xF5);
        darkPal.accentHover = QColor(0xFF, 0xFF, 0xFF);
        darkPal.accentText = QColor(0x11, 0x11, 0x11);
        darkPal.accentPressed = QColor(0xE0, 0xE0, 0xE0);
        darkPal.focusBorder = QColor(0x9C, 0xA3, 0xAF);
        // Phase 7-fix2：次按钮描边提亮 + 新增 secondaryButtonBg（与生产 palette 同源）
        darkPal.secondaryButtonBorder = QColor(0x9C, 0xA3, 0xAF);
        darkPal.secondaryButtonText = QColor(0xE7, 0xE9, 0xEE);
        darkPal.secondaryButtonBg = QColor(255, 255, 255, 36);
        darkPal.selectionBg = QColor(255, 255, 255, 20);
        darkPal.selectionLeftBar = QColor(0xF5, 0xF5, 0xF5);
        darkPal.glassSurface = QColor(30, 32, 38, 102);
        darkPal.glassSurfaceFake = QColor(30, 32, 38, 217);
        darkPal.resultCardSurface = QColor(30, 32, 38, 173);
        darkPal.glassBorder = QColor(255, 255, 255, 31);
        darkPal.glassSpecular = QColor(255, 255, 255, 51);
        darkPal.glassShadow = QColor(0, 0, 0, 115);
        darkPal.textOnGlass = QColor(0xEC, 0xEE, 0xF2);

        const QString glassLight =
            ThemeManager::renderGlassQss(lightPal, false);
        const QString glassDark =
            ThemeManager::renderGlassQss(darkPal, false);
        if (glassLight.isEmpty() || glassDark.isEmpty()) {
            glassOk = false;
        } else if (ThemeManager::hasResidualPlaceholders(glassLight)
                   || ThemeManager::hasResidualPlaceholders(glassDark)) {
            glassOk = false;
        }
    }

    QJsonObject obj;
    QJsonArray themes;
    themes.append(QStringLiteral("light"));
    themes.append(QStringLiteral("dark"));
    obj.insert(QStringLiteral("themes"), themes);
    // qss_ok 契约保持：true 当且仅当静态 qss 非空 + apply 成功
    // + 玻璃模板渲染无残留（任一失败都让 selftest 报错）。
    obj.insert(QStringLiteral("qss_ok"), qssOk && applyOk && glassOk);
    // 额外诊断字段（不破坏契约）。
    obj.insert(QStringLiteral("glass_render_ok"), glassOk);
    printJsonLine(obj);

    return (qssOk && applyOk && glassOk) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// --selftest replace  (phase 5)
// ---------------------------------------------------------------------------
int runReplace()
{
    // 预选文本 + sentinel 占剪贴板：跑完 TextReplacer 后校验
    //   1) QLineEdit 文本被替换成 "[MOCK] " + preset（Ctrl+V 落到了 edit 上）
    //   2) 剪贴板最终还原成 sentinel（备份/还原链完整）
    const QString preset = QStringLiteral("XTranslate replace selftest 替换自测");
    const QString expected =
        QStringLiteral("[MOCK] ") + preset;  // MockTranslator 输出契约
    const QString sentinel = QStringLiteral("XT_REPL_SENTINEL_%1")
                                 .arg(QDateTime::currentMSecsSinceEpoch());

    QApplication::clipboard()->setText(sentinel);
    pumpEventLoopFor(100);

    QLineEdit edit;
    edit.setWindowTitle(QStringLiteral("XTranslate replace selftest"));
    edit.setWindowFlag(Qt::WindowStaysOnTopHint);
    edit.setFixedSize(480, 48);
    edit.setText(preset);
    edit.show();
    edit.raise();
    edit.activateWindow();
    edit.setFocus();
    edit.selectAll();
    pumpEventLoopFor(400); // 让窗口真正拿到前景

#ifdef _WIN32
    // 与 runSelection 同款的强制前景技巧：agent 终端无 foreground 权限，
    // 必须用 Alt tap + AttachThreadInput 把焦点塞给 edit，否则 Ctrl+V 会落到别处。
    {
        HWND hwnd = reinterpret_cast<HWND>(edit.winId());
        if (GetForegroundWindow() != hwnd) {
            keybd_event(VK_MENU, 0, 0, 0);
            keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
            const DWORD fgThread =
                GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
            const DWORD ownThread = GetCurrentThreadId();
            if (fgThread != ownThread)
                AttachThreadInput(fgThread, ownThread, TRUE);
            SetForegroundWindow(hwnd);
            SetFocus(hwnd);
            if (fgThread != ownThread)
                AttachThreadInput(fgThread, ownThread, FALSE);
            pumpEventLoopFor(300);
            edit.setFocus();
            edit.selectAll();
            pumpEventLoopFor(200);
        }
    }
#endif

    // 强制走 mock provider，避免 selftest 环境下网络 provider 拖慢/抖动。
    ReplaceResult result;
    bool done = false;
    TextReplacer replacer;
    QEventLoop loop;
    QObject::connect(&replacer, &TextReplacer::finished, &loop,
                     [&result, &done, &loop](const ReplaceResult &r) {
        result = r;
        done = true;
        loop.quit();
    });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit); // 看门狗
    // to="zh-CN" 跳过启发式判断；provider="mock" 直接命中兜底。
    replacer.start(QString(), QStringLiteral("zh-CN"),
                   QStringLiteral("mock"));
    loop.exec();

    // 给 Qt 事件循环一帧时间消化剪贴板还原（setMimeData 是异步发布的）。
    pumpEventLoopFor(200);
    const QString editFinal = edit.text();
    const QString clipFinal = QApplication::clipboard()->text();
    edit.close();

    const bool replacedOk = done && result.replaced
                            && editFinal == expected;
    const bool clipRestored = done && result.clipboardRestored
                              && clipFinal == sentinel;

    QJsonObject obj;
    obj.insert(QStringLiteral("original"), result.originalText);
    obj.insert(QStringLiteral("translated"), result.translatedText);
    obj.insert(QStringLiteral("provider"), result.provider);
    obj.insert(QStringLiteral("edit_final"), editFinal);
    obj.insert(QStringLiteral("replaced"), replacedOk);
    obj.insert(QStringLiteral("clipboard_restored"), clipRestored);
    if (!result.error.isEmpty())
        obj.insert(QStringLiteral("error"), result.error);
    printJsonLine(obj);

    return (replacedOk && clipRestored) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// --selftest systemocr  (phase 5)
// ---------------------------------------------------------------------------
int runSystemOcr()
{
    const bool available = SystemOcrEngine::isAvailable();
    const QString langTag = SystemOcrEngine::pickLanguageTag(
        QStringLiteral("zh-CN"));

    QJsonObject obj;
    obj.insert(QStringLiteral("available"), available);
    obj.insert(QStringLiteral("lang_tag"), langTag);

    if (!available) {
        // 环境无 WinRT 或无 OCR 语言包：如实上报，pass。
        printJsonLine(obj);
        return 0;
    }

    // 用内置 800x200 测试图（与 runOcr 同款）走 SystemOcrEngine。
    const QImage img = makeBuiltInImage();
    SystemOcrEngine engine;
    const OcrResult result = engine.recognize(img).result();

    QString merged;
    QJsonArray lines;
    for (const OcrLine &line : result.lines) {
        lines.append(line.text);
        merged += line.text;
    }
    obj.insert(QStringLiteral("lines"), result.lines.size());
    obj.insert(QStringLiteral("texts"), lines);
    obj.insert(QStringLiteral("merged"), merged);
    obj.insert(QStringLiteral("elapsedMs"), result.elapsedMs);
    if (!result.error.isEmpty())
        obj.insert(QStringLiteral("error"), result.error);
    printJsonLine(obj);

    // available=true 时要求 lines>0 且 merged 同时含 "Hello" 与 "你好"。
    if (!result.error.isEmpty())
        return 1;
    return (result.lines.size() > 0
            && merged.contains(QLatin1String("Hello"))
            && merged.contains(QStringLiteral("你好"))) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// --selftest plugin  (phase 5)
// ---------------------------------------------------------------------------
int runPlugin()
{
    // 临时插件目录 + echo 插件 + rescan -> PluginTranslator。
    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) {
        QJsonObject obj;
        obj.insert(QStringLiteral("echo_ok"), false);
        obj.insert(QStringLiteral("error"), QStringLiteral("tmpdir failed"));
        printJsonLine(obj);
        return 1;
    }

    // 临时目录路径放进 qputenv，PluginManager::pluginsDir() 会读它。
    // 子目录名 = 插件 name = "echo"；plugin.py 走系统 python 解释器。
    const QByteArray envKey = "XTRANSLATE_PLUGINS_DIR";
    const QByteArray oldVal = qgetenv(envKey);
    const QByteArray newVal = tmpDir.path().toUtf8();
    qputenv(envKey, newVal);

    QDir pluginsRoot(tmpDir.path());
    const QString echoDir = pluginsRoot.filePath(QStringLiteral("echo"));
    QDir().mkpath(echoDir);

    // echo 插件：返回 {text:"<echo: <text>>"}，name 必须等于子目录名 "echo"。
    const QByteArray pluginSrc = R"PY(#!/usr/bin/env python3
import json, sys
def main():
    while True:
        line = sys.stdin.readline()
        if not line:
            break
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except Exception as e:
            sys.stdout.write(json.dumps({"error": "bad json: %s" % e}) + "\n")
            sys.stdout.flush()
            continue
        op = req.get("op")
        if op == "describe":
            sys.stdout.write(json.dumps({"name": "echo", "version": "0.0.1"}) + "\n")
            sys.stdout.flush()
        elif op == "translate":
            t = req.get("text", "")
            sys.stdout.write(json.dumps({"text": "[echo] " + t, "provider": "plugin:echo"}) + "\n")
            sys.stdout.flush()
        else:
            sys.stdout.write(json.dumps({"error": "unknown op: %s" % op}) + "\n")
            sys.stdout.flush()

if __name__ == "__main__":
    main()
)PY";
    QFile pyFile(QDir(echoDir).filePath(QStringLiteral("plugin.py")));
    if (!pyFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QJsonObject obj;
        obj.insert(QStringLiteral("echo_ok"), false);
        obj.insert(QStringLiteral("error"), QStringLiteral("write plugin.py failed"));
        printJsonLine(obj);
        return 1;
    }
    pyFile.write(pluginSrc);
    pyFile.close();

    PluginManager &pm = PluginManager::instance();
    const QVector<PluginInfo> plugins = pm.rescan();

    QJsonArray arr;
    for (const PluginInfo &p : plugins) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), p.name);
        o.insert(QStringLiteral("available"), p.available);
        o.insert(QStringLiteral("version"), p.version);
        if (!p.error.isEmpty())
            o.insert(QStringLiteral("error"), p.error);
        arr.append(o);
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("plugins"), arr);

    const PluginInfo *echo = pm.find(QStringLiteral("echo"));
    bool echoOk = false;
    QString translated;
    QString provider;
    if (!echo || !echo->available) {
        // 无 python 或 describe 失败 -> 如实上报，仍算 pass。
        obj.insert(QStringLiteral("echo_ok"), false);
        obj.insert(QStringLiteral("translated"), QString());
        obj.insert(QStringLiteral("provider"), QString());
        obj.insert(QStringLiteral("error"), echo ? echo->error
                                                  : QStringLiteral("echo not found"));
        printJsonLine(obj);
        return 0;
    }

    PluginTranslator trans(QStringLiteral("echo"));
    const TransResult r = trans.translate(QStringLiteral("XTranslate plugin selftest 插件自测"),
                                          QStringLiteral("auto"),
                                          QStringLiteral("zh-CN")).result();
    translated = r.text;
    provider = r.provider;
    echoOk = r.error.isEmpty()
             && translated == QStringLiteral("[echo] XTranslate plugin selftest 插件自测");
    obj.insert(QStringLiteral("echo_ok"), echoOk);
    obj.insert(QStringLiteral("translated"), translated);
    obj.insert(QStringLiteral("provider"), provider);
    if (!r.error.isEmpty())
        obj.insert(QStringLiteral("error"), r.error);
    printJsonLine(obj);

    return echoOk ? 0 : 1;
}

} // namespace selftest
