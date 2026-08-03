// MyMemoryTranslator - MyMemory 免费翻译记忆接口，无需密钥。
//
// GET https://api.mymemory.translated.net/get?q=...&langpair=FROM|TO
// Response: {"responseData":{"translatedText":"..."},"responseStatus":200}
//
// 限制：不支持 auto 源语言（需显式指定）；匿名配额约 5000 字符/天，
// 请求带 de=邮箱参数可提升到 50000。2026-08 作为 Bing 失效后的第二备用源接入。

#pragma once

#include "core/translate/Translator.h"

class QNetworkAccessManager;

class MyMemoryTranslator : public Translator
{
public:
    explicit MyMemoryTranslator(QNetworkAccessManager *nam);

    QString name() const override { return QStringLiteral("mymemory"); }
    bool requiresKey() const override { return false; }

    QFuture<TransResult> translate(const QString &text, const QString &from,
                                   const QString &to) override;

private:
    QNetworkAccessManager *m_nam;
};
