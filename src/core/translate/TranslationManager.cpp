#include "core/translate/TranslationManager.h"

#include "core/config/ConfigManager.h"
#include "core/translate/BingFreeTranslator.h"
#include "core/translate/GoogleFreeTranslator.h"
#include "core/translate/MockTranslator.h"
#include "core/translate/providers/BaiduTranslator.h"
#include "core/translate/providers/DeepLTranslator.h"
#include "core/translate/providers/DeepLxTranslator.h"
#include "core/translate/providers/LingvaTranslator.h"
#include "core/translate/providers/OpenAiCompatTranslator.h"
#include "core/translate/providers/TencentTranslator.h"
#include "core/translate/providers/YoudaoTranslator.h"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QPromise>

#include <functional>

TranslationManager &TranslationManager::instance()
{
    static TranslationManager mgr;
    return mgr;
}

TranslationManager::TranslationManager()
    : m_nam(std::make_unique<QNetworkAccessManager>())
{
    // Registry (order here is only the fallback when the config order is
    // incomplete; scheduling uses ConfigManager's priority list).
    m_providers.push_back(std::make_unique<GoogleFreeTranslator>(m_nam.get()));
    m_providers.push_back(std::make_unique<BingFreeTranslator>(m_nam.get()));
    m_providers.push_back(std::make_unique<DeepLTranslator>(m_nam.get()));
    m_providers.push_back(std::make_unique<BaiduTranslator>(m_nam.get()));
    m_providers.push_back(std::make_unique<YoudaoTranslator>(m_nam.get()));
    m_providers.push_back(std::make_unique<TencentTranslator>(m_nam.get()));
    m_providers.push_back(std::make_unique<OpenAiCompatTranslator>(m_nam.get()));
    m_providers.push_back(std::make_unique<DeepLxTranslator>(m_nam.get()));
    m_providers.push_back(std::make_unique<LingvaTranslator>(m_nam.get()));
    m_providers.push_back(std::make_unique<MockTranslator>());
    m_mock = m_providers.back().get();

    // Proxy: apply once at startup, then live on every proxy.* change.
    applyProxyFromConfig();
    connect(&ConfigManager::instance(), &ConfigManager::configChanged, this,
            [](const QString &path) {
        if (path.startsWith(QLatin1String("proxy")))
            applyProxyFromConfig();
    });
}

QVector<Translator *> TranslationManager::allProviders() const
{
    QVector<Translator *> list;
    list.reserve(static_cast<int>(m_providers.size()));
    for (const auto &p : m_providers)
        list.append(p.get());
    return list;
}

Translator *TranslationManager::providerByName(const QString &name) const
{
    for (const auto &p : m_providers) {
        if (p->name() == name)
            return p.get();
    }
    return nullptr;
}

void TranslationManager::applyProxyFromConfig()
{
    ConfigManager &cfg = ConfigManager::instance();
    const QString mode = cfg.stringValue(QStringLiteral("proxy.mode"));
    if (mode == QLatin1String("system")) {
        QNetworkProxyFactory::setUseSystemConfiguration(true);
    } else if (mode == QLatin1String("manual")) {
        QNetworkProxy proxy(QNetworkProxy::HttpProxy,
                            cfg.stringValue(QStringLiteral("proxy.host")),
                            static_cast<quint16>(cfg.intValue(QStringLiteral("proxy.port"))),
                            cfg.stringValue(QStringLiteral("proxy.user")),
                            cfg.stringValue(QStringLiteral("proxy.pass")));
        QNetworkProxy::setApplicationProxy(proxy);
    } else {
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    }
}

QFuture<ManagedTransResult> TranslationManager::translate(const QString &text,
                                                          const QString &from,
                                                          const QString &to,
                                                          const QString &provider)
{
    // Build the attempt chain; mock is always the terminal fallback.
    auto chain = std::make_shared<std::vector<Translator *>>();
    if (provider != QLatin1String("auto") && !provider.isEmpty()) {
        if (Translator *forced = providerByName(provider)) {
            if (forced != m_mock)
                chain->push_back(forced);
        }
    } else {
        ConfigManager &cfg = ConfigManager::instance();
        for (const QString &name : cfg.providerOrder()) {
            Translator *t = providerByName(name);
            if (!t || t == m_mock)
                continue;
            const bool enabled =
                cfg.providerConfig(name).value(QStringLiteral("enabled")).toBool();
            if (enabled && t->isConfigured())
                chain->push_back(t);
        }
    }
    chain->push_back(m_mock);

    auto promise = std::make_shared<QPromise<ManagedTransResult>>();
    promise->start();
    QFuture<ManagedTransResult> future = promise->future();

    auto state = std::make_shared<ManagedTransResult>();

    // 总预算：Phase 6-tuning。串行降级最坏 9 家 × 3s = 27s，用户体验不可接受。
    // 用 QElapsedTimer 在 tryNext 递归时检查，预算耗尽则跳过剩余真实 provider
    // 直接落 mock 兜底，确保 10s 内出结果。
    auto deadline = std::make_shared<QElapsedTimer>();
    deadline->start();
    constexpr qint64 kTotalBudgetMs = 10000;

    // Recursive sequential attempt over the chain (all on the main thread).
    auto tryNext = std::make_shared<std::function<void(size_t)>>();
    *tryNext = [this, chain, promise, state, text, from, to, tryNext,
                deadline, kTotalBudgetMs](size_t idx) {
        Translator *t = chain->at(idx);
        auto *watcher = new QFutureWatcher<TransResult>(this);
        QObject::connect(watcher, &QFutureWatcher<TransResult>::finished, this,
                         [this, chain, promise, state, watcher, tryNext, idx,
                          deadline, kTotalBudgetMs]() {
            const TransResult r = watcher->result();
            watcher->deleteLater();
            if (r.error.isEmpty()) {
                m_networkOk = (r.provider != QLatin1String("mock"));
                state->result = r;
                promise->addResult(*state);
                promise->finish();
                return;
            }
            state->errorChain << r.error;
            // 总预算耗尽且下一家不是 mock 末尾，直接跳到 mock 兜底。
            // 保留 chain->size()-1 边界，避免无限递归。
            if (deadline->elapsed() >= kTotalBudgetMs
                && idx + 1 < chain->size() - 1) {
                (*tryNext)(chain->size() - 1);
                return;
            }
            if (idx + 1 < chain->size()) {
                (*tryNext)(idx + 1);
            } else {
                // Should not happen (mock never fails) but stay safe.
                m_networkOk = false;
                state->result = r;
                promise->addResult(*state);
                promise->finish();
            }
        });
        watcher->setFuture(t->translate(text, from, to));
    };
    (*tryNext)(0);

    return future;
}
