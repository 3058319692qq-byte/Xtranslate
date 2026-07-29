// SelectionGrabber - grabs the text currently selected in ANY application.
//
// Strategy (phase 3, triggered by the Alt+X global hotkey):
//   1. Back up the clipboard (best effort: every QMimeData format is cloned,
//      plain text is always preserved).
//   2. Release any hotkey-held modifiers, then SendInput Ctrl+C.
//   3. Poll GetClipboardSequenceNumber (15 ms interval, 400 ms cap) until the
//      foreign app has published its copy.
//   4. Read the text; 300 ms later restore the clipboard backup (the delay
//      keeps slow clipboard viewers from seeing a half-written state).
//   5. If the clipboard route produced nothing, fall back to UI Automation:
//      focused element -> TextPattern -> GetSelection.
//
// finished(SelectionResult) is emitted AFTER the restore step so callers
// (popup card, --selftest selection) can rely on the clipboard being intact.
// `source` records which route succeeded ("clipboard"/"uia") for the dev
// report statistics; `copyLatencyMs` is the measured sequence-number wait.

#pragma once

#include <QObject>
#include <QString>

class QMimeData;

struct SelectionResult {
    QString text;                 // empty when both routes failed
    QString source;               // "clipboard" | "uia" | ""
    bool clipboardRestored = false;
    qint64 copyLatencyMs = 0;     // Ctrl+C -> clipboard sequence change
};

class SelectionGrabber : public QObject
{
    Q_OBJECT
public:
    explicit SelectionGrabber(QObject *parent = nullptr);

    // Asynchronous; emits finished() exactly once, then the grabber can be
    // deleted (callers typically use deleteLater in the slot).
    void start();

signals:
    void finished(const SelectionResult &result);

private:
    void onCopyArrived(bool viaClipboard);
    void restoreClipboardAndFinish();
    static QString grabViaUiAutomation();

    QMimeData *m_backup = nullptr;
    SelectionResult m_result;
};
