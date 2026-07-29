// TextReplacer - 把任意应用中当前选中的文本就地替换成译文（phase 5）。
//
// 触发：Alt+T 全局热键（HotkeyManager::defaultActions() 中 text_replace）。
// 流程：
//   1. SelectionGrabber 取焦点控件选中文本（Ctrl+C 注入 + UIA 兜底 +
//      剪贴板备份/还原已内置）。
//   2. TranslationManager 翻译（用户当前 provider 链；mock 兜底保证有结果）。
//   3. 备份当前剪贴板（再次备份，因为 grabber 已还原成用户原始内容）。
//   4. 把译文写入剪贴板。
//   5. SendInput Ctrl+V 让目标控件粘贴，覆盖原选中文本。
//   6. 400ms 后还原剪贴板备份（给慢粘贴应用留窗口）。
//   7. emit finished(ReplaceResult)，调用方按需提示或写历史。
//
// 无选中文本时直接 emit finished(replaced=false)，不做任何剪贴板操作。

#pragma once

#include <QObject>
#include <QString>

class QMimeData;

struct ReplaceResult {
    QString originalText;     // grabber 抓到的选中文本
    QString translatedText;   // 译文（失败时为空）
    QString provider;         // 实际命中的 provider（mock 兜底也算）
    bool replaced = false;            // 是否完成了粘贴替换
    bool clipboardRestored = false;   // 剪贴板是否还原成 step3 时的状态
    QString error;            // 翻译链失败时的错误链（join 后）
};

class TextReplacer : public QObject
{
    Q_OBJECT
public:
    explicit TextReplacer(QObject *parent = nullptr);

    // 异步；emit finished() 恰好一次，调用方通常 deleteLater。
    // from/to 为空时由 TranslationManager 走默认链路。
    void start(const QString &from = QString(),
               const QString &to = QString(),
               const QString &provider = QStringLiteral("auto"));

signals:
    void finished(const ReplaceResult &result);

private:
    void onGrabbed(const class SelectionResult &grabbed);
    void onTranslated(const class ManagedTransResult &translated);
    void pasteAndRestore();
    void restoreClipboardAndFinish();

    QMimeData *m_backup = nullptr;
    ReplaceResult m_result;
    QString m_from;
    QString m_to;
    QString m_provider;
};
