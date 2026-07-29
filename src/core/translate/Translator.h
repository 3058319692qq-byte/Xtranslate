// Translator - abstract translation interface.
//
// Language codes are BCP-47: auto / zh-CN / zh-TW / en / ja / ko / fr / de /
// es / ru. Individual providers map these to their own dialects internally.

#pragma once

#include <QFuture>
#include <QString>

struct TransResult {
    QString text;            // translated text (empty on failure)
    QString provider;        // registry name ("google" / "deepl" / "mock" / ...)
    qint64 elapsedMs = 0;
    QString error;           // empty on success
};

class Translator
{
public:
    virtual ~Translator() = default;

    virtual QString name() const = 0;

    // Phase 4 registry metadata: providers that need credentials report
    // requiresKey()=true; isConfigured() is false until the user fills the
    // required fields, in which case the provider is visible in settings but
    // greyed out and skipped by the scheduling chain.
    virtual bool requiresKey() const { return false; }
    virtual bool isConfigured() const { return true; }

    // Must be invoked from the thread owning the shared QNetworkAccessManager
    // (the main thread); the future completes on that same thread.
    virtual QFuture<TransResult> translate(const QString &text,
                                           const QString &from,
                                           const QString &to) = 0;
};
