#include "app/DocShots.h"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

#include "core/config/ConfigManager.h"
#include "ui/MainWindow.h"
#include "ui/overlay/ControlBar.h"
#include "ui/overlay/OverlayWindow.h"
#include "ui/popup/PopupCard.h"
#include "ui/settings/SettingsWindow.h"
#include "ui/theme/ThemeManager.h"
#include "ui/tray/TrayManager.h"
#include "core/capture/ScreenCapturer.h"

#include <cstdio>

#include <QApplication>
#include <QEventLoop>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

namespace docshots {

namespace {

// Same helper as SelfTest.cpp: pump the loop so layered/DWM frames land.
void pumpEventLoopFor(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// Neutral sample sentences - no user data, no real desktop content.
const QString kSampleEn =
    QStringLiteral("The quick brown fox jumps over the lazy dog.");
const QString kSampleZh = QStringLiteral("敏捷的棕色狐狸跳过了懒狗。");
const QString kSampleEn2 =
    QStringLiteral("Hello world, this is a sample sentence.");
const QString kSampleZh2 = QStringLiteral("你好世界，这是一段示例文字。");

// Full-screen solid-color backdrop: every captured pixel that is not the
// target window belongs to THIS window, never to the real desktop.
// Topmost on purpose: a normal (non-topmost) backdrop can end up BELOW
// other running topmost apps (video players etc.), leaking them into the
// capture. Scenes therefore make their target window topmost as well and
// show it AFTER the backdrop, so it stacks above within the topmost band.
QWidget *makeBackdrop()
{
    auto *w = new QWidget(nullptr, Qt::FramelessWindowHint
                                       | Qt::WindowStaysOnTopHint);
    w->setAttribute(Qt::WA_ShowWithoutActivating);
    w->setAutoFillBackground(true);
    QPalette pal = w->palette();
    const bool dark = ThemeManager::instance().isDark();
    pal.setColor(QPalette::Window,
                 dark ? QColor(0x2B, 0x2D, 0x31) : QColor(0xDD, 0xE3, 0xEA));
    w->setPalette(pal);
    w->setGeometry(QApplication::primaryScreen()->geometry());
    w->show();
    return w;
}

// White placeholder "document" window used as the translation source for the
// popup / overlay scenes (plain sample English on white - stands in for
// whatever real content the user would normally translate over).
QWidget *makePlaceholderDoc(const QRect &geom, const QStringList &lines,
                            int topMargin, int lineGap)
{
    auto *w = new QWidget(nullptr, Qt::FramelessWindowHint | Qt::Tool
                                       | Qt::WindowStaysOnTopHint);
    w->setAttribute(Qt::WA_ShowWithoutActivating);
    w->setAutoFillBackground(true);
    QPalette pal = w->palette();
    pal.setColor(QPalette::Window, Qt::white);
    w->setPalette(pal);
    w->setGeometry(geom);

    auto *layout = new QVBoxLayout(w);
    layout->setContentsMargins(24, topMargin, 24, 16);
    layout->setSpacing(lineGap);
    QFont font(QStringLiteral("Segoe UI"));
    font.setPointSize(13);
    for (const QString &line : lines) {
        auto *label = new QLabel(line, w);
        label->setFont(font);
        label->setStyleSheet(QStringLiteral("color: #1F2937;"));
        label->setWordWrap(true);
        layout->addWidget(label);
    }
    layout->addStretch(1);
    w->show();
    return w;
}

// Capture `logicalRect` and save. Returns 0 / 3 (contract in the header).
int captureAndSave(const QRect &logicalRect, const QString &outPath)
{
    const QImage shot = ScreenCapturer::grabLogicalRect(logicalRect);
    if (shot.isNull()) {
        std::fprintf(stderr, "[shot] capture failed for %d,%d %dx%d\n",
                     logicalRect.x(), logicalRect.y(), logicalRect.width(),
                     logicalRect.height());
        return 3;
    }
    if (!shot.save(outPath, "PNG")) {
        std::fprintf(stderr, "[shot] save failed: %s\n",
                     outPath.toUtf8().constData());
        return 3;
    }
    std::fprintf(stdout, "SHOT_OK %s %dx%d\n", outPath.toUtf8().constData(),
                 shot.width(), shot.height());
    return 0;
}

// -------------------------------------------------------------------------
// Scenes
// -------------------------------------------------------------------------

int shotMain(const QString &outPath)
{
    QWidget *backdrop = makeBackdrop();

    MainWindow window;
    // 截图脚手架：临时置顶，确保主窗压在置顶背景窗之上（同为 topmost，
    // 后 show 者居上）；生产路径不受影响（仅 --shot 进程内）。
    window.setWindowFlag(Qt::WindowStaysOnTopHint, true);
    const QRect screen = QApplication::primaryScreen()->geometry();
    window.move(screen.center() - QPoint(window.width() / 2,
                                         window.height() / 2));
    window.show();
    window.raise();
    window.activateWindow();
    pumpEventLoopFor(600); // DWM backdrop + first layout pass

    // Inject deterministic sample text. blockSignals on the source edit so
    // the 600ms debounce auto-translate never overwrites the sample result
    // (no network dependency, reproducible pixels).
    if (auto *src = window.findChild<QPlainTextEdit *>(
            QStringLiteral("sourceEdit"))) {
        src->blockSignals(true);
        src->setPlainText(kSampleEn);
        src->blockSignals(false);
    }
    if (auto *res = window.findChild<QPlainTextEdit *>(
            QStringLiteral("resultEdit")))
        res->setPlainText(kSampleZh);
    pumpEventLoopFor(500);

    const int rc = captureAndSave(window.frameGeometry().adjusted(
                                      -24, -24, 24, 24),
                                  outPath);
    delete backdrop;
    return rc;
}

int shotSettings(const QString &outPath)
{
    QWidget *backdrop = makeBackdrop();

    SettingsWindow::open(nullptr);
    SettingsWindow *settings = nullptr;
    for (QWidget *top : QApplication::topLevelWidgets()) {
        if ((settings = qobject_cast<SettingsWindow *>(top)))
            break;
    }
    if (!settings) {
        delete backdrop;
        return 3;
    }
    const QRect screen = QApplication::primaryScreen()->geometry();
    settings->move(screen.center() - QPoint(settings->width() / 2,
                                            settings->height() / 2));
    // 同 shotMain：临时置顶压过置顶背景窗；setWindowFlag 会重建窗口并隐藏，
    // 需要再 show 一次。
    settings->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    settings->show();
    settings->raise();
    pumpEventLoopFor(800);

    const int rc = captureAndSave(settings->frameGeometry().adjusted(
                                      -24, -24, 24, 24),
                                  outPath);
    delete backdrop;
    return rc;
}

int shotPopup(const QString &outPath)
{
    QWidget *backdrop = makeBackdrop();

    // App-created white "document" as the selection source (rule: never a
    // real desktop or user file behind the card).
    const QRect screen = QApplication::primaryScreen()->geometry();
    const QRect docRect(screen.center() - QPoint(320, 200), QSize(640, 400));
    QWidget *doc = makePlaceholderDoc(
        docRect,
        {kSampleEn, kSampleEn2,
         QStringLiteral("Select any text and press the hotkey to translate.")},
        20, 10);
    pumpEventLoopFor(200);

    auto *card = new PopupCard(kSampleEn);
    card->popupAt(docRect.topLeft() + QPoint(150, 80));
    pumpEventLoopFor(400); // fade-in done (180ms) + Acrylic applied

    // Deterministic result: overwrite whatever the real translation attempt
    // produced (offline machines would otherwise show 翻译失败).
    if (auto *result = card->findChild<QLabel *>(QStringLiteral("resultLabel")))
        result->setText(kSampleZh);
    const auto buttons = card->findChildren<QPushButton *>(
        QStringLiteral("iconButton"));
    for (QPushButton *btn : buttons)
        btn->setEnabled(true);
    card->adjustSize();
    pumpEventLoopFor(400);

    const QRect capture = docRect.united(card->frameGeometry())
                              .adjusted(-16, -16, 16, 16);
    const int rc = captureAndSave(capture, outPath);
    card->close();
    delete doc;
    delete backdrop;
    return rc;
}

int shotOverlay(const QString &outPath)
{
    QWidget *backdrop = makeBackdrop();

    // Placeholder document = the "screen content being translated". The two
    // source lines sit where the overlay panels will land, like a real run.
    const QRect screen = QApplication::primaryScreen()->geometry();
    const QRect docRect(screen.center() - QPoint(300, 170), QSize(600, 340));
    QWidget *doc = makePlaceholderDoc(docRect, {kSampleEn, kSampleEn2}, 30, 90);
    pumpEventLoopFor(200);

    OverlayWindow overlay(docRect);
    QVector<OverlayGroup> groups;
    OverlayGroup a;
    a.rect = QRectF(20, 24, 540, 44);
    a.lineHeight = 22;
    a.sourceText = kSampleEn;
    a.translatedText = kSampleZh;
    a.loading = false;
    OverlayGroup b;
    // 与占位文档第二行（margin 30 + 行高≈25 + 间距 90）对齐，面板完整盖住源文行。
    b.rect = QRectF(20, 138, 540, 48);
    b.lineHeight = 22;
    b.sourceText = kSampleEn2;
    b.translatedText = kSampleZh2;
    b.loading = false;
    groups << a << b;
    overlay.setGroups(groups);
    overlay.show();

    ControlBar bar;
    bar.dockTo(docRect);
    bar.show();
    pumpEventLoopFor(600); // layered overlay frame + bar Acrylic up

    const QRect capture = docRect.united(bar.frameGeometry())
                              .adjusted(-16, -16, 16, 16);
    const int rc = captureAndSave(capture, outPath);
    delete doc;
    delete backdrop;
    return rc;
}

int shotTray(const QString &outPath)
{
    QWidget *backdrop = makeBackdrop();

    // TrayManager builds the production menu (five actions + 设置/退出 with
    // live hotkey text). Pop it over the backdrop and crop to the menu rect:
    // no taskbar, no other tray icons in frame.
    TrayManager tray;
    QMenu *menu = nullptr;
    for (QWidget *top : QApplication::topLevelWidgets()) {
        if ((menu = qobject_cast<QMenu *>(top)))
            break;
    }
    if (!menu) {
        delete backdrop;
        return 3;
    }
    const QRect screen = QApplication::primaryScreen()->geometry();
    menu->popup(screen.center());
    pumpEventLoopFor(500);

    const int rc = captureAndSave(menu->frameGeometry().adjusted(
                                      -12, -12, 12, 12),
                                  outPath);
    menu->close();
    delete backdrop;
    return rc;
}

} // namespace

int runShot(const QStringList &args)
{
    // WIN32 子系统默认无控制台：同 --selftest 的处理，管道重定向保留不动，
    // 终端直启时 AttachConsole+freopen 把 SHOT_OK/错误输出接回父控制台。
#ifdef _WIN32
    const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    const DWORD ft = (hOut && hOut != INVALID_HANDLE_VALUE)
                         ? GetFileType(hOut) : FILE_TYPE_UNKNOWN;
    if (ft != FILE_TYPE_PIPE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            freopen("CONOUT$", "w", stdout);
            freopen("CONOUT$", "w", stderr);
        }
    }
#endif

    // Contract: --shot <scene> --out <png>  (invoked by gen_docs_shots.ps1;
    // combine with --theme light|dark for the light/dark variants).
    const int shotIdx = args.indexOf(QStringLiteral("--shot"));
    const int outIdx = args.indexOf(QStringLiteral("--out"));
    if (shotIdx < 0 || shotIdx + 1 >= args.size() || outIdx < 0
        || outIdx + 1 >= args.size()) {
        std::fprintf(stderr, "usage: --shot <main|settings|popup|overlay|tray>"
                             " --out <png> [--theme light|dark]\n");
        return 2;
    }
    const QString scene = args.at(shotIdx + 1);
    const QString outPath = args.at(outIdx + 1);

    if (scene == QLatin1String("main"))
        return shotMain(outPath);
    if (scene == QLatin1String("settings"))
        return shotSettings(outPath);
    if (scene == QLatin1String("popup"))
        return shotPopup(outPath);
    if (scene == QLatin1String("overlay"))
        return shotOverlay(outPath);
    if (scene == QLatin1String("tray"))
        return shotTray(outPath);

    std::fprintf(stderr, "unknown --shot scene: '%s'\n",
                 scene.toUtf8().constData());
    return 2;
}

} // namespace docshots
