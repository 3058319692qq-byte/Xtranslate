#include "app/DocShots.h"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

#include "core/config/ConfigManager.h"
#include "core/storage/HistoryStore.h"
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
#include <QComboBox>
#include <QEventLoop>
#include <QLabel>
#include <QListWidget>
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

    // TrayManager builds the production menu (five actions + 目标语言/设置/
    // 退出 with live hotkey text). Pop it over the backdrop and crop to the
    // menu rect: no taskbar, no other tray icons in frame.
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

    // v0.7.1：展开"目标语言"子菜单（带子菜单的唯一 action），证据截图
    // 同时包含主菜单 + 子菜单（当前目标语言项带勾选）。
    QMenu *subMenu = nullptr;
    for (QAction *a : menu->actions()) {
        if (a->menu()) {
            menu->setActiveAction(a);
            subMenu = a->menu();
            subMenu->popup(menu->frameGeometry().topRight() + QPoint(6, 60));
            break;
        }
    }
    pumpEventLoopFor(500);

    QRect rect = menu->frameGeometry();
    if (subMenu && subMenu->isVisible())
        rect = rect.united(subMenu->frameGeometry());
    const int rc = captureAndSave(rect.adjusted(-12, -12, 12, 12), outPath);
    if (subMenu)
        subMenu->close();
    menu->close();
    delete backdrop;
    return rc;
}

// v0.7.1：设置窗指定页截图（navRow: 2=翻译服务 4=朗读）。
// 翻译服务页验证选中态/间距；朗读页可选先切到日语验证"无专用嗓音用
// 默认"提示（ttsLang 非空时）。
int shotSettingsPage(const QString &outPath, int navRow, const QString &ttsLang)
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
    if (auto *nav = settings->findChild<QListWidget *>(
            QStringLiteral("settingsNav")))
        nav->setCurrentRow(navRow);
    if (!ttsLang.isEmpty()) {
        if (auto *langCombo = settings->findChild<QComboBox *>(
                QStringLiteral("ttsLangCombo"))) {
            const int idx = langCombo->findData(ttsLang);
            if (idx >= 0)
                langCombo->setCurrentIndex(idx);
        }
    }
    const QRect screen = QApplication::primaryScreen()->geometry();
    settings->move(screen.center() - QPoint(settings->width() / 2,
                                            settings->height() / 2));
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

// v0.7.1 BUG-B 证据：真实翻译一次 → 历史侧栏立即出现该条。
// 不阻断 textChanged：走生产链路 debounce→translate→写库→changed→侧栏。
// 断言 HistoryStore 行数 +1（mock 兑底不写库时如实报 3）。
int shotHistoryLive(const QString &outPath)
{
    QWidget *backdrop = makeBackdrop();

    MainWindow window;
    window.setWindowFlag(Qt::WindowStaysOnTopHint, true);
    const QRect screen = QApplication::primaryScreen()->geometry();
    window.move(screen.center() - QPoint(window.width() / 2,
                                         window.height() / 2));
    window.show();
    window.raise();
    window.activateWindow();
    pumpEventLoopFor(600);

    const int before = HistoryStore::instance().count();
    if (auto *src = window.findChild<QPlainTextEdit *>(
            QStringLiteral("sourceEdit")))
        src->setPlainText(kSampleEn); // 信号不阻断 → 真实翻译链路
    pumpEventLoopFor(6000); // debounce 600ms + 网络往返 + 写库刷新
    const int after = HistoryStore::instance().count();
    std::fprintf(stdout, "HISTORY_LIVE before=%d after=%d\n", before, after);

    const int rc = captureAndSave(window.frameGeometry().adjusted(
                                      -24, -24, 24, 24),
                                  outPath);
    delete backdrop;
    return (rc == 0 && after == before + 1) ? 0 : 3;
}

// v0.7.1 BUG-B 证据：历史为空时的占位文案（外层脚本负责把真库暂时
// 移开再恢复）。不输入任何文本，只拍空态主窗 + 侧栏占位项。
int shotHistoryEmpty(const QString &outPath)
{
    QWidget *backdrop = makeBackdrop();

    MainWindow window;
    window.setWindowFlag(Qt::WindowStaysOnTopHint, true);
    const QRect screen = QApplication::primaryScreen()->geometry();
    window.move(screen.center() - QPoint(window.width() / 2,
                                         window.height() / 2));
    window.show();
    window.raise();
    pumpEventLoopFor(800);

    const int rc = captureAndSave(window.frameGeometry().adjusted(
                                      -24, -24, 24, 24),
                                  outPath);
    delete backdrop;
    return rc;
}

// v0.7.1 托盘切目标语言联动证据：模拟托盘子菜单点选（写 config
// translate.target_lang=ja）→ 主窗目标语言下拉即时跟随。截图 + stdout
// 断言，结束后恢复原配置。
int shotTraySync(const QString &outPath)
{
    QWidget *backdrop = makeBackdrop();

    MainWindow window;
    window.setWindowFlag(Qt::WindowStaysOnTopHint, true);
    const QRect screen = QApplication::primaryScreen()->geometry();
    window.move(screen.center() - QPoint(window.width() / 2,
                                         window.height() / 2));
    window.show();
    window.raise();
    pumpEventLoopFor(600);

    auto *combo = window.findChild<QComboBox *>(QStringLiteral("tgtLangCombo"));
    if (!combo) {
        delete backdrop;
        return 3;
    }
    const QString before = combo->currentData().toString();
    // 与托盘菜单项 triggered 完全同源的生产路径：写配置 → configChanged。
    ConfigManager::instance().setValue(QStringLiteral("translate.target_lang"),
                                       QStringLiteral("ja"));
    pumpEventLoopFor(300);
    const QString after = combo->currentData().toString();
    std::fprintf(stdout, "TRAY_SYNC before=%s after=%s\n",
                 before.toUtf8().constData(), after.toUtf8().constData());

    const int rc = captureAndSave(window.frameGeometry().adjusted(
                                      -24, -24, 24, 24),
                                  outPath);
    // 恢复原配置，不污染用户环境。
    ConfigManager::instance().setValue(QStringLiteral("translate.target_lang"),
                                       before.isEmpty()
                                           ? QStringLiteral("zh-CN") : before);
    delete backdrop;
    return (rc == 0 && after == QLatin1String("ja")) ? 0 : 3;
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
        std::fprintf(stderr, "usage: --shot <main|settings|popup|overlay|tray"
                             "|providers|ttspage|historylive|historyempty"
                             "|traysync> --out <png> [--theme light|dark]\n");
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
    // v0.7.1 证据场景
    if (scene == QLatin1String("providers"))
        return shotSettingsPage(outPath, 2, QString());
    if (scene == QLatin1String("ttspage"))
        return shotSettingsPage(outPath, 4, QStringLiteral("ja"));
    if (scene == QLatin1String("historylive"))
        return shotHistoryLive(outPath);
    if (scene == QLatin1String("historyempty"))
        return shotHistoryEmpty(outPath);
    if (scene == QLatin1String("traysync"))
        return shotTraySync(outPath);

    std::fprintf(stderr, "unknown --shot scene: '%s'\n",
                 scene.toUtf8().constData());
    return 2;
}

} // namespace docshots
