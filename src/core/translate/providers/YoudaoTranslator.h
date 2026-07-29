// YoudaoTranslator - 有道智云文本翻译 (appKey + appSecret, v3 sha256 sign).
//
// POST https://openapi.youdao.com/api form fields q/from/to/appKey/salt/
// sign/signType=v3/curtime. sign = sha256(appKey + input + salt + curtime +
// appSecret) where input = q when len(q) <= 20, otherwise
// first10 + len(q) + last10.

#pragma once

#include "core/translate/Translator.h"

class QNetworkAccessManager;

class YoudaoTranslator : public Translator
{
public:
    explicit YoudaoTranslator(QNetworkAccessManager *nam);

    QString name() const override { return QStringLiteral("youdao"); }
    bool requiresKey() const override { return true; }
    bool isConfigured() const override;

    QFuture<TransResult> translate(const QString &text, const QString &from,
                                   const QString &to) override;

    // Official input truncation: len>20 -> first10 + len + last10.
    static QString truncateInput(const QString &q);
    // Lowercase sha256 hex of appKey+input+salt+curtime+appSecret.
    static QString sign(const QString &appKey, const QString &input,
                        const QString &salt, const QString &curtime,
                        const QString &appSecret);
    // BCP-47 -> Youdao codes (zh-CHS / zh-CHT, rest identical).
    static QString mapLang(const QString &bcp47);

private:
    QNetworkAccessManager *m_nam;
};
