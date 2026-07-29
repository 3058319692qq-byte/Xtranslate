// DeepLxTranslator - self-hosted DeepLX endpoint (custom baseUrl).
//
// POST {baseUrl} with JSON {"text","source_lang","target_lang"} (DeepL-style
// upper-case language codes); the typical deployment URL already ends with
// /translate. Response: {"code":200,"data":"..."}.

#pragma once

#include "core/translate/Translator.h"

class QNetworkAccessManager;

class DeepLxTranslator : public Translator
{
public:
    explicit DeepLxTranslator(QNetworkAccessManager *nam);

    QString name() const override { return QStringLiteral("deeplx"); }
    bool requiresKey() const override { return false; } // endpoint, not a key
    bool isConfigured() const override;                 // baseUrl required

    QFuture<TransResult> translate(const QString &text, const QString &from,
                                   const QString &to) override;

private:
    QNetworkAccessManager *m_nam;
};
