// OpenAiCompatTranslator - OpenAI-compatible chat/completions endpoints.
//
// Works with any {baseUrl}/chat/completions service (OpenAI, DeepSeek, 智谱,
// 火山方舟, Gemini's OpenAI-compat endpoint, ...). System prompt:
// "You are a translator. Translate from {from} to {to}. Output only the
// translation."

#pragma once

#include "core/translate/Translator.h"

class QNetworkAccessManager;

class OpenAiCompatTranslator : public Translator
{
public:
    explicit OpenAiCompatTranslator(QNetworkAccessManager *nam);

    QString name() const override { return QStringLiteral("openai"); }
    bool requiresKey() const override { return true; }
    bool isConfigured() const override;

    QFuture<TransResult> translate(const QString &text, const QString &from,
                                   const QString &to) override;

    // Normalizes the endpoint: strips trailing '/', appends
    // "/chat/completions" unless the base already ends with it.
    static QString endpointFor(const QString &baseUrl);
    // Readable language name for the prompt ("Simplified Chinese", ...).
    static QString langName(const QString &bcp47);

private:
    QNetworkAccessManager *m_nam;
};
