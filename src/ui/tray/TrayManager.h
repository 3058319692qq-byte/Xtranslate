// TrayManager - system tray icon + menu (phase 3).
//
// The icon is painted in code (Phase 7-fix1: dark-gray #202020 rounded
// square with a white "译" glyph) and doubles as the main-window icon.
//
// Menu: the five hotkey actions (with their shortcuts displayed; shortcut
// context is WidgetShortcut so the GLOBAL hotkeys stay the only trigger and
// nothing fires twice) + separator + 设置 (opens SettingsWindow, phase 4) +
// 退出. Double-clicking the tray icon toggles the main window.
// 退出 emits quitRequested; the owner performs the real teardown.
// The displayed shortcuts follow ConfigManager("hotkeys.*") live.

#pragma once

#include <QHash>
#include <QIcon>
#include <QObject>

class QAction;
class QMenu;
class QSystemTrayIcon;

class TrayManager : public QObject
{
    Q_OBJECT
public:
    explicit TrayManager(QObject *parent = nullptr);
    ~TrayManager() override;

    // Painted product icon (tray + main window + app).
    static QIcon appIcon();

    bool isAvailable() const;

    // Balloon notification anchored at the tray icon.
    // category ∈ {"","capture_ocr","capture_translate","selection","replace",
    //              "translate_failed"}；
    // 空字符串 = 不分类（始终显示，给系统级错误用）。
    // 走 ConfigManager 的 notifications 开关：总开关关=全关；逐项在总开关开
    // 时生效；category="" 不受开关影响。
    void showBubble(const QString &title, const QString &message,
                    const QString &category = QString());

signals:
    void screenshotTranslateRequested();
    void screenshotOcrRequested();
    void selectionTranslateRequested();
    void toggleMainWindowRequested();
    void speakClipboardRequested();
    void settingsRequested();
    void quitRequested();

private:
    void refreshShortcuts();

    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_settingsAction = nullptr;
    QHash<QString, QAction *> m_actionById;
};
