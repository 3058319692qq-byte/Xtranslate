// TranslationManager - provider registry + configurable scheduling (phase 4).
//
// Singleton owning the shared QNetworkAccessManager and all Translator
// instances (google/bing free providers, the seven key-based phase-4
// providers, and the mock terminal fallback).
//
// translate() walks a chain derived from ConfigManager: the user's priority
// order filtered to enabled + configured providers, with mock always appended
// as the guaranteed fallback (mock never appears in the UI priority list).
// A user-forced provider name replaces the config chain for that request.
//
// The QNetworkProxy application-wide setting follows the config proxy block
// (none/system/manual) and is re-applied live on configChanged("proxy.*").

#pragma once

#include "core/translate/Translator.h"

#include <QObject>
#include <QStringList>
#include <QVector>

#include <memory>
#include <vector>

class QNetworkAccessManager;

struct ManagedTransResult {
    TransResult result;          // final outcome (mock guarantees success)
    QStringList errorChain;      // one entry per failed provider
};

class TranslationManager : public QObject
{
    Q_OBJECT
public:
    static TranslationManager &instance();

    // provider: "auto" (config-driven chain) or a registry name ("google",
    // "deepl", ...). Must be called from the main thread; the future
    // finishes there too.
    QFuture<ManagedTransResult> translate(const QString &text,
                                          const QString &from,
                                          const QString &to,
                                          const QString &provider = QStringLiteral("auto"));

    // Full registry including mock (self-test/settings enumeration).
    QVector<Translator *> allProviders() const;
    Translator *providerByName(const QString &name) const;

    // True when the last completed request was served by a real network
    // provider, false when it fell back to mock.
    bool networkOk() const { return m_networkOk; }

    // Reads the proxy block from ConfigManager and applies it process-wide.
    static void applyProxyFromConfig();

private:
    TranslationManager();

    std::unique_ptr<QNetworkAccessManager> m_nam;
    std::vector<std::unique_ptr<Translator>> m_providers; // registry order
    Translator *m_mock = nullptr;
    bool m_networkOk = false;
};
