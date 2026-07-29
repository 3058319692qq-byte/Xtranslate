// HotkeyManager - global hotkeys via Win32 RegisterHotKey (phase 3).
//
// Singleton. Registers system-wide hotkeys with a NULL hwnd so WM_HOTKEY is
// posted to the GUI thread's message queue; a QAbstractNativeEventFilter
// installed on the QApplication picks the messages up and invokes the bound
// callback on the main thread.
//
// QKeySequence -> (MOD_*, VK) conversion supports Ctrl/Alt/Shift/Win
// combinations with letters, digits, F1-F24 and common navigation keys.
// A registration conflict (key grabbed by another process) does NOT abort:
// the action lands on the failed list (queryable, used by --selftest hotkey
// and for the tray bubble warning) and the remaining actions still register.

#pragma once

#include <QAbstractNativeEventFilter>
#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class HotkeyManager : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    // One well-known action: stable id + product label + default binding.
    struct ActionSpec {
        QString actionId;
        QString label;       // product (Chinese) label, reused by the tray menu
        QKeySequence sequence;
    };

    static HotkeyManager &instance();

    // The six default bindings, in menu order:
    //   screenshot_translate Alt+D / screenshot_ocr Alt+S / selection_translate
    //   Alt+X / toggle_main Alt+M / speak_clipboard Alt+R / text_replace Alt+T.
    static QVector<ActionSpec> defaultActions();

    // Registers `seq` globally and binds `callback` (invoked on the GUI
    // thread). Returns false when RegisterHotKey fails (conflict) or the
    // sequence cannot be mapped; the action is then kept on the failed list.
    bool registerAction(const QString &actionId, const QKeySequence &seq,
                        std::function<void()> callback);

    // Re-registers an existing action under a new sequence (phase 4 settings).
    bool rebind(const QString &actionId, const QKeySequence &seq);

    void unregisterAll();

    // "actionId (sequence)" for every binding whose registration failed.
    QStringList failedActions() const;

    // QKeySequence -> Win32 modifiers + virtual key. Returns false for
    // sequences without a mappable key. MOD_NOREPEAT is always added.
    static bool toWin32(const QKeySequence &seq, quint32 *outMods, quint32 *outVk);

    bool nativeEventFilter(const QByteArray &eventType, void *message,
                           qintptr *result) override;

private:
    HotkeyManager();
    ~HotkeyManager() override;

    struct Entry {
        QString actionId;
        QKeySequence sequence;
        std::function<void()> callback;
        int win32Id = 0;         // id passed to RegisterHotKey
        bool registered = false;
    };

    Entry *findEntry(const QString &actionId);

    QVector<Entry> m_entries;
    int m_nextWin32Id = 1;
};
