#include "core/hotkey/HotkeyManager.h"

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

namespace {

#ifdef _WIN32
// Maps a Qt key (modifiers stripped) to a Win32 virtual-key code.
// Returns 0 when the key is not supported for global registration.
quint32 qtKeyToVk(int qtKey)
{
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z)
        return static_cast<quint32>('A' + (qtKey - Qt::Key_A));
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9)
        return static_cast<quint32>('0' + (qtKey - Qt::Key_0));
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24)
        return static_cast<quint32>(VK_F1 + (qtKey - Qt::Key_F1));

    switch (qtKey) {
    case Qt::Key_Space:     return VK_SPACE;
    case Qt::Key_Escape:    return VK_ESCAPE;
    case Qt::Key_Tab:       return VK_TAB;
    case Qt::Key_Return:
    case Qt::Key_Enter:     return VK_RETURN;
    case Qt::Key_Backspace: return VK_BACK;
    case Qt::Key_Insert:    return VK_INSERT;
    case Qt::Key_Delete:    return VK_DELETE;
    case Qt::Key_Home:      return VK_HOME;
    case Qt::Key_End:       return VK_END;
    case Qt::Key_PageUp:    return VK_PRIOR;
    case Qt::Key_PageDown:  return VK_NEXT;
    case Qt::Key_Left:      return VK_LEFT;
    case Qt::Key_Right:     return VK_RIGHT;
    case Qt::Key_Up:        return VK_UP;
    case Qt::Key_Down:      return VK_DOWN;
    case Qt::Key_Pause:     return VK_PAUSE;
    case Qt::Key_Print:     return VK_SNAPSHOT;
    default:                return 0;
    }
}
#endif

} // namespace

HotkeyManager &HotkeyManager::instance()
{
    static HotkeyManager mgr;
    return mgr;
}

QVector<HotkeyManager::ActionSpec> HotkeyManager::defaultActions()
{
    return {
        {QStringLiteral("screenshot_translate"),
         QCoreApplication::translate("HotkeyManager", "截图翻译"),
         QKeySequence(QStringLiteral("Alt+D"))},
        {QStringLiteral("screenshot_ocr"),
         QCoreApplication::translate("HotkeyManager", "截图OCR"),
         QKeySequence(QStringLiteral("Alt+S"))},
        {QStringLiteral("selection_translate"),
         QCoreApplication::translate("HotkeyManager", "划词翻译"),
         QKeySequence(QStringLiteral("Alt+X"))},
        {QStringLiteral("toggle_main"),
         QCoreApplication::translate("HotkeyManager", "显示/隐藏主窗"),
         QKeySequence(QStringLiteral("Alt+M"))},
        {QStringLiteral("speak_clipboard"),
         QCoreApplication::translate("HotkeyManager", "朗读剪贴板"),
         QKeySequence(QStringLiteral("Alt+R"))},
        {QStringLiteral("text_replace"),
         QCoreApplication::translate("HotkeyManager", "文本替换"),
         QKeySequence(QStringLiteral("Alt+T"))},
    };
}

HotkeyManager::HotkeyManager()
{
    // Thread messages (NULL-hwnd WM_HOTKEY) are handed to native event
    // filters by Qt's Windows event dispatcher before DispatchMessage.
    QCoreApplication::instance()->installNativeEventFilter(this);
}

HotkeyManager::~HotkeyManager()
{
    unregisterAll();
}

bool HotkeyManager::toWin32(const QKeySequence &seq, quint32 *outMods,
                            quint32 *outVk)
{
#ifdef _WIN32
    if (seq.isEmpty())
        return false;
    const QKeyCombination combo = seq[0];

    quint32 mods = MOD_NOREPEAT;
    const Qt::KeyboardModifiers qmods = combo.keyboardModifiers();
    if (qmods.testFlag(Qt::ControlModifier)) mods |= MOD_CONTROL;
    if (qmods.testFlag(Qt::AltModifier))     mods |= MOD_ALT;
    if (qmods.testFlag(Qt::ShiftModifier))   mods |= MOD_SHIFT;
    if (qmods.testFlag(Qt::MetaModifier))    mods |= MOD_WIN;

    const quint32 vk = qtKeyToVk(combo.key());
    if (vk == 0)
        return false;

    if (outMods) *outMods = mods;
    if (outVk)   *outVk = vk;
    return true;
#else
    Q_UNUSED(seq); Q_UNUSED(outMods); Q_UNUSED(outVk);
    return false;
#endif
}

bool HotkeyManager::registerAction(const QString &actionId,
                                   const QKeySequence &seq,
                                   std::function<void()> callback)
{
    // Re-registration under the same id replaces the old binding.
    if (Entry *existing = findEntry(actionId)) {
#ifdef _WIN32
        if (existing->registered)
            UnregisterHotKey(nullptr, existing->win32Id);
#endif
        m_entries.removeIf([&actionId](const Entry &e) {
            return e.actionId == actionId;
        });
    }

    Entry entry;
    entry.actionId = actionId;
    entry.sequence = seq;
    entry.callback = std::move(callback);
    entry.win32Id = m_nextWin32Id++;
    entry.registered = false;

#ifdef _WIN32
    quint32 mods = 0, vk = 0;
    if (toWin32(seq, &mods, &vk))
        entry.registered = RegisterHotKey(nullptr, entry.win32Id, mods, vk) != FALSE;
#endif

    m_entries.append(std::move(entry));
    return m_entries.last().registered;
}

bool HotkeyManager::rebind(const QString &actionId, const QKeySequence &seq)
{
    Entry *entry = findEntry(actionId);
    if (!entry)
        return false;
    std::function<void()> callback = entry->callback;
    return registerAction(actionId, seq, std::move(callback));
}

void HotkeyManager::unregisterAll()
{
#ifdef _WIN32
    for (const Entry &entry : m_entries) {
        if (entry.registered)
            UnregisterHotKey(nullptr, entry.win32Id);
    }
#endif
    m_entries.clear();
}

QStringList HotkeyManager::failedActions() const
{
    QStringList failed;
    for (const Entry &entry : m_entries) {
        if (!entry.registered) {
            failed.append(QStringLiteral("%1 (%2)").arg(
                entry.actionId, entry.sequence.toString(QKeySequence::PortableText)));
        }
    }
    return failed;
}

bool HotkeyManager::nativeEventFilter(const QByteArray &eventType,
                                      void *message, qintptr *result)
{
    Q_UNUSED(result);
#ifdef _WIN32
    if (eventType != "windows_generic_MSG")
        return false;
    const MSG *msg = static_cast<const MSG *>(message);
    if (msg->message != WM_HOTKEY)
        return false;
    const int id = static_cast<int>(msg->wParam);
    for (const Entry &entry : m_entries) {
        if (entry.registered && entry.win32Id == id) {
            if (entry.callback)
                entry.callback();
            return true;
        }
    }
#else
    Q_UNUSED(eventType); Q_UNUSED(message);
#endif
    return false;
}

HotkeyManager::Entry *HotkeyManager::findEntry(const QString &actionId)
{
    for (Entry &entry : m_entries) {
        if (entry.actionId == actionId)
            return &entry;
    }
    return nullptr;
}
