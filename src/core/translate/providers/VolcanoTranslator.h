// VolcanoTranslator - 火山翻译（字节跳动）免费 Web 接口，无需密钥。
//
// POST https://translate.volcengine.com/crx/translate/v1/
// Body: {"text":"...","source_language":"auto|en","target_language":"zh"}
// Response: {"translation":"...","detected_language":"en",
//            "base_resp":{"status_code":0,"status_message":""}}
//
// 2026-08 接入：Bing 免费 token 端点（edge.microsoft.com/translate/auth）
// 被微软下线（404）后的国内直连替代方案，支持 auto 源语言检测。

#pragma once

#include "core/translate/Translator.h"

class QNetworkAccessManager;

class VolcanoTranslator : public Translator
{
public:
    explicit VolcanoTranslator(QNetworkAccessManager *nam);

    QString name() const override { return QStringLiteral("volcano"); }
    bool requiresKey() const override { return false; }

    QFuture<TransResult> translate(const QString &text, const QString &from,
                                   const QString &to) override;

    // BCP-47 -> 火山代码。注意：zh-CN 必须映射为 zh（服务端对 zh-CN/zh-Hans
    // 会原样返回不翻译）；zh-TW 映射为 zh-Hant。
    static QString mapLang(const QString &bcp47);

private:
    QNetworkAccessManager *m_nam;
};
