// SettingsWindow - left nav + stacked pages settings dialog (phase 4).
//
// Pages: 常规 / 快捷键 / 翻译服务 / OCR / TTS / 代理 / 关于. Every control
// writes ConfigManager immediately (change = save); hotkey Apply goes through
// HotkeyManager::rebind and reports success/conflict inline. The provider
// page reorders the priority list, toggles enabled flags, edits credentials
// and can live-test any provider ("hello" -> inline result).
//
// Opened from the tray 设置 entry and the main-window menu; a single
// instance is reused (open() raises the existing window).

#pragma once

#include <QDialog>
#include <QHash>
#include <QKeySequenceEdit>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QStackedWidget;
class QVBoxLayout;
class QFormLayout;

class SettingsWindow : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsWindow(QWidget *parent = nullptr);

    // Shows (or raises) the shared instance.
    static void open(QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;

private:
    // ---- page builders ----
    QWidget *buildGeneralPage();
    QWidget *buildHotkeyPage();
    QWidget *buildProvidersPage();
    QWidget *buildOcrPage();
    QWidget *buildTtsPage();
    QWidget *buildProxyPage();
    QWidget *buildPluginsPage();
    QWidget *buildAboutPage();

    // ---- plugins page helpers ----
    void reloadPluginList();

    // ---- providers page helpers ----
    void reloadProviderList(const QString &selectName = QString());
    void rebuildProviderForm(const QString &providerName);
    void moveProvider(int delta);
    void testProvider(const QString &providerName);
    static QString providerDisplayName(const QString &name);

    // ---- hotkey page helpers ----
    void applyHotkey(const QString &actionId);
    void restoreDefaultHotkeys();
    // Phase 7: 应用/刷新顶层窗口 DWM backdrop（与 MainWindow 同策略）。
    void applyBackdrop();

    // nav + stack
    QListWidget *m_nav = nullptr;
    QStackedWidget *m_stack = nullptr;

    // hotkey page state (actionId -> row widgets)
    struct HotkeyRow {
        QKeySequenceEdit *edit = nullptr;
        QLabel *status = nullptr;
    };
    QHash<QString, HotkeyRow> m_hotkeyRows;

    // providers page state
    QListWidget *m_providerList = nullptr;
    QWidget *m_providerFormHost = nullptr;
    QFormLayout *m_providerForm = nullptr;
    QLabel *m_providerTestResult = nullptr;
    QPushButton *m_providerTestButton = nullptr;
    bool m_providerListLoading = false;

    // proxy page state
    QRadioButton *m_proxyNone = nullptr;
    QRadioButton *m_proxySystem = nullptr;
    QRadioButton *m_proxyManual = nullptr;
    QLineEdit *m_proxyHost = nullptr;
    QSpinBox *m_proxyPort = nullptr;
    QLineEdit *m_proxyUser = nullptr;
    QLineEdit *m_proxyPass = nullptr;
    QLabel *m_proxyTestResult = nullptr;

    // plugins page state
    QListWidget *m_pluginList = nullptr;
    QLabel *m_pluginDirLabel = nullptr;
    QLabel *m_pluginRescanResult = nullptr;

    // notifications page state（嵌在常规页内）
    QCheckBox *m_notifMaster = nullptr;
    QHash<QString, QCheckBox *> m_notifCategoryChecks;
};
