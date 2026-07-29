// DeepLTranslator - DeepL API v2 (auth key required).
//
// Free keys carry the ":fx" suffix and go to api-free.deepl.com; pro keys go
// to api.deepl.com (auto-detected). POST /v2/translate with JSON body and
// "Authorization: DeepL-Auth-Key <key>".

#pragma once

#include "core/translate/Translator.h"

class QNetworkAccessManager;

class DeepLTranslator : public Translator
{
public:
    explicit DeepLTranslator(QNetworkAccessManager *nam);

    QString name() const override { return QStringLiteral("deepl"); }
    bool requiresKey() const override { return true; }
    bool isConfigured() const override;

    QFuture<TransResult> translate(const QString &text, const QString &from,
                                   const QString &to) override;

    // BCP-47 -> DeepL codes; target english needs the regional EN-US form.
    static QString mapLang(const QString &bcp47, bool isTarget);
    // api-free.deepl.com for ":fx" keys, api.deepl.com otherwise.
    static QString hostForKey(const QString &authKey);

private:
    QNetworkAccessManager *m_nam;
};
