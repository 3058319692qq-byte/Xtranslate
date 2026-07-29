// LangCatalog - 语言表单一来源（v0.7.1）。
//
// 主窗源/目标语言下拉、托盘"目标语言"子菜单、设置-朗读页语言选择共用此表，
// 保证三处语言集合与显示文案完全同源。label 沿用 "MainWindow" 翻译上下文
// （历史 .ts/.qm 中已有这批词条，避免迁移丢翻译）。

#pragma once

#include <QCoreApplication>
#include <QString>

struct LangEntry {
    const char *code;   // BCP-47
    const char *label;  // 源文案（zh_CN），运行时经 translate() 出词条
};

// "auto" 仅源语言下拉使用；目标语言/托盘/TTS 场景请跳过首项。
inline const LangEntry *langCatalog(int *count)
{
    static const LangEntry kLangs[] = {
        {"auto",  QT_TRANSLATE_NOOP("MainWindow", "自动检测")},
        {"zh-CN", QT_TRANSLATE_NOOP("MainWindow", "简体中文")},
        {"zh-TW", QT_TRANSLATE_NOOP("MainWindow", "繁体中文")},
        {"en",    QT_TRANSLATE_NOOP("MainWindow", "英语")},
        {"ja",    QT_TRANSLATE_NOOP("MainWindow", "日语")},
        {"ko",    QT_TRANSLATE_NOOP("MainWindow", "韩语")},
        {"fr",    QT_TRANSLATE_NOOP("MainWindow", "法语")},
        {"de",    QT_TRANSLATE_NOOP("MainWindow", "德语")},
        {"es",    QT_TRANSLATE_NOOP("MainWindow", "西班牙语")},
        {"ru",    QT_TRANSLATE_NOOP("MainWindow", "俄语")},
    };
    if (count)
        *count = static_cast<int>(sizeof(kLangs) / sizeof(kLangs[0]));
    return kLangs;
}

// 语言 code -> 本地化显示名（未收录时原样返回 code）。
inline QString langDisplayName(const QString &code)
{
    int n = 0;
    const LangEntry *langs = langCatalog(&n);
    for (int i = 0; i < n; ++i) {
        if (code == QLatin1String(langs[i].code))
            return QCoreApplication::translate("MainWindow", langs[i].label);
    }
    return code;
}
