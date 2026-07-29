#include "core/replace/TextReplacer.h"

#include "core/selection/SelectionGrabber.h"
#include "core/translate/TranslationManager.h"

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

#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QMimeData>
#include <QTimer>
#include <QVector>

namespace {

constexpr int kPasteDelayMs = 400;   // 粘贴后等目标控件完成替换再还原剪贴板

// 与 SelectionGrabber 同款的 best-effort 快照：私有格式可能无法还原，
// 但至少 plain text 走得通；此处只用于短暂占用剪贴板。
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

// SendInput Ctrl+V 到当前前台窗口（用户触发热键时仍是目标控件）。
// 先松开所有修饰键，避免 Alt+T 的 Alt 还按着把 Ctrl+V 变成 Ctrl+Alt+V。
void sendCtrlV()
{
    QVector<INPUT> batch;
    sendKey(VK_MENU, false, &batch);
    sendKey(VK_LMENU, false, &batch);
    sendKey(VK_RMENU, false, &batch);
    sendKey(VK_SHIFT, false, &batch);
    sendKey(VK_LWIN, false, &batch);
    sendKey(VK_RWIN, false, &batch);
    sendKey(VK_CONTROL, true, &batch);
    sendKey('V', true, &batch);
    sendKey('V', false, &batch);
    sendKey(VK_CONTROL, false, &batch);
    SendInput(static_cast<UINT>(batch.size()), batch.data(), sizeof(INPUT));
}
#endif // _WIN32

} // namespace

TextReplacer::TextReplacer(QObject *parent)
    : QObject(parent)
{
}

void TextReplacer::start(const QString &from, const QString &to,
                         const QString &provider)
{
    m_from = from;
    m_to = to;
    m_provider = provider;

    // SelectionGrabber 内部已做剪贴板备份/还原，结束后剪贴板回到用户原始内容。
    auto *grabber = new SelectionGrabber(this);
    connect(grabber, &SelectionGrabber::finished,
            this, &TextReplacer::onGrabbed);
    grabber->start();
}

void TextReplacer::onGrabbed(const SelectionResult &grabbed)
{
    // grabber 是 this 的子对象，但流程结束后立即清理避免悬留（自身对象
    // 生命周期由调用方管理：MainWindow 用 this 父子关系 + deleteLater；
    // selftest 用栈对象，finished 后退出作用域自动销毁）。
    auto *grabber = qobject_cast<SelectionGrabber *>(sender());
    if (grabber)
        grabber->deleteLater();

    m_result.originalText = grabbed.text;
    if (grabbed.text.trimmed().isEmpty()) {
        // 没有选中文本：不触碰剪贴板，直接结束。
        emit finished(m_result);
        return;
    }

    // 翻译（异步）。from/to 为空时交给 TranslationManager 默认链路。
    QString src = m_from;
    QString dst = m_to;
    if (src.isEmpty())
        src = QStringLiteral("auto");
    if (dst.isEmpty()) {
        // CJK -> en，否则 -> zh-CN，与 runTranslate 的启发式保持一致。
        bool hasCjk = false;
        for (QChar ch : grabbed.text) {
            const ushort u = ch.unicode();
            if ((u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3040 && u <= 0x30FF)
                || (u >= 0xAC00 && u <= 0xD7AF)) {
                hasCjk = true;
                break;
            }
        }
        dst = hasCjk ? QStringLiteral("en") : QStringLiteral("zh-CN");
    }

    auto *watcher = new QFutureWatcher<ManagedTransResult>(this);
    connect(watcher, &QFutureWatcher<ManagedTransResult>::finished,
            this, [this, watcher]() {
        watcher->deleteLater();
        onTranslated(watcher->result());
    });
    watcher->setFuture(TranslationManager::instance().translate(
        grabbed.text, src, dst, m_provider));
}

void TextReplacer::onTranslated(const ManagedTransResult &translated)
{
    if (translated.result.error.isEmpty() && !translated.result.text.isEmpty()) {
        m_result.translatedText = translated.result.text;
        m_result.provider = translated.result.provider;
        pasteAndRestore();
    } else {
        // 翻译链全失败（mock 也失败才会到这里，正常不会）：不替换，直接结束。
        m_result.error = translated.errorChain.join(QStringLiteral(" | "));
        emit finished(m_result);
    }
}

void TextReplacer::pasteAndRestore()
{
#ifdef _WIN32
    // 备份当前剪贴板（此时 grabber 已还原成用户原始内容）。
    m_backup = cloneClipboard();

    // 写入译文，SendInput Ctrl+V，400ms 后还原。
    QApplication::clipboard()->setText(m_result.translatedText);

    // 给目标控件一帧时间消化 setText（Qt 剪贴板是异步发布的）。
    QTimer::singleShot(50, this, [this]() {
        sendCtrlV();
        QTimer::singleShot(kPasteDelayMs, this,
                           &TextReplacer::restoreClipboardAndFinish);
    });
#else
    // 非 Windows：跳过粘贴步骤，仅记录译文。
    m_result.replaced = false;
    m_result.clipboardRestored = true;
    emit finished(m_result);
#endif
}

void TextReplacer::restoreClipboardAndFinish()
{
#ifdef _WIN32
    if (m_backup) {
        QApplication::clipboard()->setMimeData(m_backup);
        m_backup = nullptr;
        m_result.clipboardRestored = true;
    }
    m_result.replaced = true;
#else
    m_result.replaced = false;
    m_result.clipboardRestored = true;
#endif
    emit finished(m_result);
}
