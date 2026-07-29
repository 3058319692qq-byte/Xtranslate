// LingvaTranslator - Lingva Translate instance (custom baseUrl, no key).
//
// GET {baseUrl}/api/v1/{from}/{to}/{percent-encoded text}. Response:
// {"translation":"..."}. Default public instance: https://lingva.ml.

#pragma once

#include "core/translate/Translator.h"

class QNetworkAccessManager;

class LingvaTranslator : public Translator
{
public:
    explicit LingvaTranslator(QNetworkAccessManager *nam);

    QString name() const override { return QStringLiteral("lingva"); }
    bool requiresKey() const override { return false; }
    bool isConfigured() const override; // baseUrl required

    QFuture<TransResult> translate(const QString &text, const QString &from,
                                   const QString &to) override;

    // BCP-47 -> Lingva codes (zh / zh_HANT, rest identical).
    static QString mapLang(const QString &bcp47);

private:
    QNetworkAccessManager *m_nam;
};
