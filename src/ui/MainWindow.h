// MainWindow - "输入翻译" (text input translation) main window.
//
// Layout : menu bar (设置 / 历史侧栏)
//          + toolbar (source lang / swap / target lang / 截图OCR / 截图翻译 /
//          provider)
//          + source QPlainTextEdit
//          + read-only result QPlainTextEdit with floating 复制/朗读 buttons
//          + status bar (provider + elapsed / error)
//          + collapsible history dock (search / favorite / refill).
// Behavior: 600 ms debounce auto-translate, Ctrl+Enter immediate translate,
//           swap button exchanges languages and moves the result back to
//           the source box.
// Phase 2 : 截图OCR -> region select -> capture -> OCR -> OcrResultDialog;
//           截图翻译 -> region select -> ScreenTranslateController overlay.
// Phase 3 : owns the system tray + global hotkeys; close = hide to tray.
// Phase 4 : hotkeys come from ConfigManager (settings page rebinds live),
//           styling moved to the theme QSS, successful input translations
//           are recorded into HistoryStore (scene 'input').

#pragma once

#include "core/storage/HistoryStore.h"
#include "core/translate/TranslationManager.h"

#include <QMainWindow>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class QToolButton;
class HistorySidebar;
class TrayManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

public slots:
    // Phase 3 entry points shared by tray menu + global hotkeys.
    void startScreenshotOcr();
    void startScreenshotTranslate();
    void startSelectionTranslate();
    void toggleVisibility();
    void speakClipboard();
    void quitApplication();
    void openSettings();
    // Phase 5: 文本替换（Alt+T），就地用译文覆盖当前选中文本。
    void startTextReplace();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void onSourceTextChanged();
    void triggerTranslate();
    void swapLanguages();
    void copyResult();
    void speakResult();
    // Phase 7-fix2b BUG9：清空原文+译文+状态栏复位"就绪"，焦点回原文框。
    // 只清当前输入/译文：不动历史记录、不动配置、不触发新翻译。
    void clearInput();
    void onHistoryEntryActivated(const HistoryEntry &entry);
    // Phase 7-fix1：翻译字体可调，配置改变即时刷新
    void onFontConfigChanged(const QString &path);

private:
    void buildUi();
    void buildMenu();
    void buildHistoryDock();
    void positionCopyButton();
    // Phase 7-fix2b BUG9：清空钮浮在 sourceEdit 右上角（与 copyButton 同款
    // 浮动定位模式），resize 时跟随。
    void positionClearButton();
    void initTrayAndHotkeys();
    QString currentProviderKey() const;
    void runOcrOnRegion(const QRect &globalLogicalRect);
    // Phase 7: 应用/刷新顶层窗口 DWM backdrop（Mica/Acrylic）。
    // 在 showEvent 与 ThemeManager 信号回调中调用，reduce_transparency
    // 开启时由 WinBackdrop 自动 no-op 走假玻璃回退。
    void applyBackdrop();
    // Phase 7-fix1：从 ConfigManager 读 ui.font 并应用到 sourceEdit/resultEdit。
    // 字号直接用配置 pt；颜色 result_color="theme" 时用主题 textOnGlass，
    // "custom" 时用 result_color_custom。sourceEdit 颜色始终跟随主题 text。
    void applyFontConfig();

    QComboBox *m_fromCombo = nullptr;
    QComboBox *m_toCombo = nullptr;
    QComboBox *m_providerCombo = nullptr;
    QToolButton *m_swapButton = nullptr;
    QPushButton *m_shotOcrButton = nullptr;
    QPushButton *m_shotTranslateButton = nullptr;
    QPlainTextEdit *m_sourceEdit = nullptr;
    QPlainTextEdit *m_resultEdit = nullptr;
    QPushButton *m_copyButton = nullptr;
    QPushButton *m_speakButton = nullptr;
    // Phase 7-fix2b BUG9：清空输入图标钮（Lucide eraser，浮在原文框右上角）。
    QPushButton *m_clearButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_autoLangHint = nullptr;  // Phase 7-fix2 BUG6：自动检测限制说明灰字
    QTimer *m_debounce = nullptr;
    quint64 m_requestSeq = 0;   // drop stale responses

    // Phase 3 state
    TrayManager *m_tray = nullptr;
    bool m_quitting = false;        // true only via tray 退出
    bool m_hideHintShown = false;   // "已隐藏到托盘" bubble shown once

    // Phase 4 state
    HistorySidebar *m_historyDock = nullptr;
};
