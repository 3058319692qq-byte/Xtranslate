#include "core/selection/SelectionGrabber.h"

#ifdef _WIN32
#  ifndef WINVER
#    define WINVER 0x0A00
#  endif
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0A00
#  endif
// NO WIN32_LEAN_AND_MEAN here: UIAutomation.h needs the COM base headers
// (objbase.h defines the `interface` macro) that the lean windows.h skips.
#  define NOMINMAX
#  include <windows.h>
#  include <UIAutomation.h>
#endif

#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QMimeData>
#include <QTimer>
#include <QVector>

namespace {

constexpr int kPollIntervalMs = 15;   // clipboard sequence-number poll step
constexpr int kPollTimeoutMs = 400;   // give up waiting for the foreign copy
constexpr int kRestoreDelayMs = 300;  // grace period before restoring backup

// Best-effort clipboard snapshot; at minimum plain text survives the round
// trip. Some private formats cannot be re-set, that is acceptable.
QMimeData *cloneClipboard()
{
    const QMimeData *src = QApplication::clipboard()->mimeData();
    auto *copy = new QMimeData;
    if (!src)
        return copy;
    const QStringList formats = src->formats();
    for (const QString &format : formats)
        copy->setData(format, src->data(format));
    if (src->hasText() && !copy->hasText())
        copy->setText(src->text());
    return copy;
}

#ifdef _WIN32

void sendKey(WORD vk, bool down, QVector<INPUT> *batch)
{
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    batch->append(input);
}

// The Alt+X hotkey leaves Alt physically held; inject key-ups for every
// modifier first, otherwise the copy would arrive as Ctrl+Alt+C.
//
// A dummy VK_NONAME press goes first: RegisterHotKey swallows the X, so the
// target app would otherwise see a *lone Alt tap* and arm its menu bar /
// key-tips (new Notepad), where the subsequent 'C' gets eaten as an
// accelerator instead of copying. Any keydown while Alt is still held
// cancels that heuristic.
void sendCtrlC(bool dismissKeytipsFirst)
{
    QVector<INPUT> batch;
    if (dismissKeytipsFirst) {
        // Retry path: key-tips may already be armed (XAML key-tips are not
        // reported by GetGUIThreadInfo) - Esc dismisses them, and is a no-op
        // for plain editors/browsers with a selection.
        sendKey(VK_ESCAPE, true, &batch);
        sendKey(VK_ESCAPE, false, &batch);
    }
    sendKey(VK_NONAME, true, &batch);
    sendKey(VK_NONAME, false, &batch);
    sendKey(VK_MENU, false, &batch);
    sendKey(VK_LMENU, false, &batch);
    sendKey(VK_RMENU, false, &batch);
    sendKey(VK_SHIFT, false, &batch);
    sendKey(VK_LWIN, false, &batch);
    sendKey(VK_RWIN, false, &batch);
    sendKey(VK_CONTROL, true, &batch);
    sendKey('C', true, &batch);
    sendKey('C', false, &batch);
    sendKey(VK_CONTROL, false, &batch);
    SendInput(static_cast<UINT>(batch.size()), batch.data(), sizeof(INPUT));
}

// Win32 menu loops (classic apps) ARE observable; dismiss them before the
// first copy attempt.
void escapeMenuModeIfActive()
{
    HWND fg = GetForegroundWindow();
    if (!fg)
        return;
    GUITHREADINFO gti = {};
    gti.cbSize = sizeof(gti);
    const DWORD tid = GetWindowThreadProcessId(fg, nullptr);
    if (GetGUIThreadInfo(tid, &gti)
        && (gti.flags & (GUI_INMENUMODE | GUI_SYSTEMMENUMODE
                         | GUI_POPUPMENUMODE))) {
        QVector<INPUT> batch;
        sendKey(VK_ESCAPE, true, &batch);
        sendKey(VK_ESCAPE, false, &batch);
        SendInput(static_cast<UINT>(batch.size()), batch.data(),
                  sizeof(INPUT));
    }
}

#endif // _WIN32

} // namespace

SelectionGrabber::SelectionGrabber(QObject *parent)
    : QObject(parent)
{
}

void SelectionGrabber::start()
{
#ifdef _WIN32
    // Debug/measurement hook: XT_SELECTION_FORCE_UIA=1 skips the clipboard
    // route entirely so the UI Automation fallback can be exercised and
    // measured in real applications (dev-report evidence only).
    if (qEnvironmentVariableIsSet("XT_SELECTION_FORCE_UIA")) {
        onCopyArrived(false); // clipboard untouched, no backup to restore
        return;
    }

    m_backup = cloneClipboard();

    const DWORD seqBefore = GetClipboardSequenceNumber();
    escapeMenuModeIfActive();
    sendCtrlC(false);

    // Poll for the foreign app's copy without blocking the GUI thread.
    // Midway (~200 ms) one retry is injected: covers apps that ignored the
    // first Ctrl+C because a menu loop was still winding down.
    auto *timer = new QTimer(this);
    auto *elapsed = new QElapsedTimer;
    auto *retried = new bool(false);
    elapsed->start();
    timer->setInterval(kPollIntervalMs);
    connect(timer, &QTimer::timeout, this,
            [this, timer, elapsed, retried, seqBefore]() {
        if (GetClipboardSequenceNumber() != seqBefore) {
            m_result.copyLatencyMs = elapsed->elapsed();
            timer->stop();
            timer->deleteLater();
            delete elapsed;
            delete retried;
            onCopyArrived(true);
            return;
        }
        if (!*retried && elapsed->elapsed() >= kPollTimeoutMs / 2) {
            *retried = true;
            escapeMenuModeIfActive();
            sendCtrlC(true);
        }
        if (elapsed->elapsed() >= kPollTimeoutMs) {
            m_result.copyLatencyMs = elapsed->elapsed();
            timer->stop();
            timer->deleteLater();
            delete elapsed;
            delete retried;
            onCopyArrived(false);
        }
    });
    timer->start();
#else
    m_result.clipboardRestored = true;
    emit finished(m_result);
#endif
}

void SelectionGrabber::onCopyArrived(bool viaClipboard)
{
    if (viaClipboard) {
        const QString text = QApplication::clipboard()->text();
        if (!text.isEmpty()) {
            m_result.text = text;
            m_result.source = QStringLiteral("clipboard");
        }
    }

    if (m_result.text.isEmpty()) {
        // Clipboard route failed (console windows, copy-protected fields...):
        // ask UI Automation for the focused element's selected text.
        const QString uiaText = grabViaUiAutomation();
        if (!uiaText.isEmpty()) {
            m_result.text = uiaText;
            m_result.source = QStringLiteral("uia");
        }
    }

    if (!viaClipboard) {
        // Nothing was copied, the clipboard is untouched - no restore needed.
        delete m_backup;
        m_backup = nullptr;
        m_result.clipboardRestored = true;
        emit finished(m_result);
        return;
    }

    QTimer::singleShot(kRestoreDelayMs, this,
                       &SelectionGrabber::restoreClipboardAndFinish);
}

void SelectionGrabber::restoreClipboardAndFinish()
{
    if (m_backup) {
        // setMimeData takes ownership of the backup.
        QApplication::clipboard()->setMimeData(m_backup);
        m_backup = nullptr;
        m_result.clipboardRestored = true;
    }
    emit finished(m_result);
}

QString SelectionGrabber::grabViaUiAutomation()
{
#ifdef _WIN32
    // The GUI thread is already OLE-initialized by Qt; S_FALSE is fine.
    const HRESULT coInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool needUninit = coInit == S_OK;

    QString result;
    IUIAutomation *automation = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr,
                                   CLSCTX_INPROC_SERVER,
                                   __uuidof(IUIAutomation),
                                   reinterpret_cast<void **>(&automation)))
        && automation) {
        IUIAutomationElement *focused = nullptr;
        if (SUCCEEDED(automation->GetFocusedElement(&focused)) && focused) {
            IUIAutomationTextPattern *textPattern = nullptr;
            if (SUCCEEDED(focused->GetCurrentPatternAs(
                    UIA_TextPatternId, __uuidof(IUIAutomationTextPattern),
                    reinterpret_cast<void **>(&textPattern)))
                && textPattern) {
                IUIAutomationTextRangeArray *ranges = nullptr;
                if (SUCCEEDED(textPattern->GetSelection(&ranges)) && ranges) {
                    int count = 0;
                    ranges->get_Length(&count);
                    for (int i = 0; i < count; ++i) {
                        IUIAutomationTextRange *range = nullptr;
                        if (SUCCEEDED(ranges->GetElement(i, &range)) && range) {
                            BSTR bstr = nullptr;
                            if (SUCCEEDED(range->GetText(-1, &bstr)) && bstr) {
                                result += QString::fromWCharArray(bstr);
                                SysFreeString(bstr);
                            }
                            range->Release();
                        }
                    }
                    ranges->Release();
                }
                textPattern->Release();
            }
            focused->Release();
        }
        automation->Release();
    }

    if (needUninit)
        CoUninitialize();
    return result;
#else
    return QString();
#endif
}
