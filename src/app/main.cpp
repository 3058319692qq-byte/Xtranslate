// XTranslate - application entry point (phase 5).
//
// Product display name : X翻译
// Internal ASCII name   : XTranslate
//
// Responsibilities:
//   * Per-monitor-v2 DPI awareness + UTF-8 console output.
//   * QApplication identity (version 0.5.0).
//   * --selftest env|ocr|translate|capture|overlay|hotkey|tts|selection
//     |config|db|providers|theme|replace|systemocr|plugin
//     (one JSON line on stdout, exit 0 = pass).
//   * WIN32 subsystem since phase 5: --selftest reattaches the parent
//     console via AttachConsole(ATTACH_PARENT_PROCESS) + freopen so JSON
//     output and exit code stay intact; GUI runs stay black-window-free.
//   * No arguments: install the configured UI translator, apply the
//     configured theme, show the input-translation main window
//     (tray-resident: closing hides to the tray, only 退出 quits).

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

#include <cstdio>

#include <QApplication>
#include <QStringList>
#include <QTimer>
#include <QTranslator>

#include "app/DocShots.h"
#include "app/SelfTest.h"
#include "core/config/ConfigManager.h"
#include "ui/MainWindow.h"
#include "ui/theme/ThemeManager.h"

int main(int argc, char *argv[])
{
#ifdef _WIN32
    // Must run before any Qt/GUI initialization.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // Emit UTF-8 to the console so JSON (incl. "X翻译") is well-formed.
    SetConsoleOutputCP(CP_UTF8);
#endif

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("XTranslate"));
    QApplication::setApplicationDisplayName(QStringLiteral("X翻译"));
    QApplication::setOrganizationName(QStringLiteral("XTranslate"));
    // v0.7.2：版本号统一（与 CMakeLists.txt / VERSIONINFO.rc / XTranslate.iss /
    // README 五处对齐，selftest env 输出 version 字段供验收自动核对）。
    QApplication::setApplicationVersion(QStringLiteral("0.7.2"));

    // --selftest <mode> [args...]
    const QStringList args = QCoreApplication::arguments();
    const int stIdx = args.indexOf(QStringLiteral("--selftest"));
    if (stIdx >= 0) {
        // WIN32 子系统下默认无控制台，C 运行时 stdout 可能未正确绑定。
        // 按 GetFileType(STD_OUTPUT_HANDLE) 区分三种场景：
        //   FILE_TYPE_PIPE  : 父进程已重定向 stdout 到管道（PowerShell 捕获、
        //                     deploy.ps1 重定向）→ 保留不动，直接用 C 运行时 stdout。
        //   FILE_TYPE_CHAR  : 父控制台句柄（终端直启）→ AttachConsole + freopen
        //                     把 stdout 重新绑定到 CONOUT$，否则 fwrite 落空。
        //   其他（无效句柄）: explorer 双击无控制台 → AttachConsole 失败则静默继续。
        // 任何路径下 exit code 都由 return 决定（底线契约，绝不破坏）。
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
        const QString mode = (stIdx + 1 < args.size()) ? args.at(stIdx + 1) : QString();
        const QStringList rest = args.mid(stIdx + 2);
        if (mode == QLatin1String("env"))
            return selftest::runEnv();
        if (mode == QLatin1String("ocr"))
            return selftest::runOcr(rest);
        if (mode == QLatin1String("translate"))
            return selftest::runTranslate(rest);
        if (mode == QLatin1String("capture"))
            return selftest::runCapture(rest);
        if (mode == QLatin1String("overlay"))
            return selftest::runOverlay();
        if (mode == QLatin1String("hotkey"))
            return selftest::runHotkey();
        if (mode == QLatin1String("tts"))
            return selftest::runTts();
        if (mode == QLatin1String("selection"))
            return selftest::runSelection();
        if (mode == QLatin1String("config"))
            return selftest::runConfig();
        if (mode == QLatin1String("db"))
            return selftest::runDb();
        if (mode == QLatin1String("providers"))
            return selftest::runProviders();
        if (mode == QLatin1String("theme"))
            return selftest::runTheme();
        if (mode == QLatin1String("replace"))
            return selftest::runReplace();
        if (mode == QLatin1String("systemocr"))
            return selftest::runSystemOcr();
        if (mode == QLatin1String("plugin"))
            return selftest::runPlugin();
        std::fprintf(stderr, "unknown --selftest mode: '%s'\n", mode.toUtf8().constData());
        return 2;
    }

    // UI language follows the config (zh_CN default = untranslated source;
    // runtime hot-switch is out of scope for this phase, restart applies it).
    const QString uiLang = ConfigManager::instance().uiLanguage();
    auto *translator = new QTranslator(&app);
    if (translator->load(QStringLiteral(":/i18n/xtranslate_%1.qm").arg(uiLang)))
        QApplication::installTranslator(translator);

    // Theme (light/dark/system) from the config; live-updates afterwards.
    ThemeManager::instance().applyFromConfig();

    // Phase 7: --theme <light|dark|system> 覆盖配置主题（仅启动时，gallery
    // 截图用）。不带 --theme 时行为不变（仍走 applyFromConfig）。
    const int themeIdx = args.indexOf(QStringLiteral("--theme"));
    if (themeIdx >= 0 && themeIdx + 1 < args.size())
        ThemeManager::instance().apply(args.at(themeIdx + 1));

    // Phase 9-fix1: --shot <scene> --out <png> 隐藏截图模式（docs/ README
    // 展示图重截，纯色背景零桌面隐私，见 DocShots.h）。同 --selftest
    // 一样属测试脚手架，不影响无参数的正常 GUI 启动路径。
    if (args.contains(QStringLiteral("--shot")))
        return docshots::runShot(args);

    // Tray-resident GUI: closing the main window must not end the process.
    QApplication::setQuitOnLastWindowClosed(false);

    MainWindow window;
    // 开机启动通过注册表带 --minimized 启动，登录后直接驻留托盘。
    if (args.contains(QStringLiteral("--minimized")))
        window.hide();
    else
        window.show();

    // Testability hook (used by the phase-4 GUI evidence scripts): menu
    // clicks are unreliable on a busy shared desktop, so --open-settings
    // opens the settings dialog right after startup. No other side effects.
    if (args.contains(QStringLiteral("--open-settings")))
        QTimer::singleShot(600, &window, &MainWindow::openSettings);

    return app.exec();
}
