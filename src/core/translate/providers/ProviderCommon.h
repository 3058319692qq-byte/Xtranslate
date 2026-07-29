// ProviderCommon - shared plumbing for the phase-4 HTTP translation providers.
//
// Each provider follows the same pattern as GoogleFreeTranslator: build a
// request on the main thread, finish a shared QPromise<TransResult> from the
// QNetworkReply::finished lambda. These helpers centralize the promise/timer
// boilerplate and the ConfigManager field access.

#pragma once

#include "core/config/ConfigManager.h"
#include "core/translate/Translator.h"

#include <QElapsedTimer>
#include <QNetworkReply>
#include <QPromise>

#include <memory>

namespace provider {

// 单家 provider 的总传输超时。Phase 6-tuning：8s → 3s，配合 TranslationManager
// 的 10s 总预算，让串行降级链路在最坏情况下 ~10s 内落到 mock 兜底。
constexpr int kTimeoutMs = 3000;

// One in-flight request: shared promise + elapsed timer + provider name.
struct Ctx {
    std::shared_ptr<QPromise<TransResult>> promise;
    std::shared_ptr<QElapsedTimer> timer;
    QString name;
};

inline Ctx makeCtx(const QString &name)
{
    Ctx ctx;
    ctx.promise = std::make_shared<QPromise<TransResult>>();
    ctx.promise->start();
    ctx.timer = std::make_shared<QElapsedTimer>();
    ctx.timer->start();
    ctx.name = name;
    return ctx;
}

inline void finishError(const Ctx &ctx, const QString &message)
{
    TransResult result;
    result.provider = ctx.name;
    result.elapsedMs = ctx.timer->elapsed();
    result.error = QStringLiteral("%1: %2").arg(ctx.name, message);
    ctx.promise->addResult(result);
    ctx.promise->finish();
}

inline void finishText(const Ctx &ctx, const QString &text)
{
    TransResult result;
    result.provider = ctx.name;
    result.elapsedMs = ctx.timer->elapsed();
    if (text.isEmpty())
        result.error = QStringLiteral("%1: empty translation").arg(ctx.name);
    else
        result.text = text;
    ctx.promise->addResult(result);
    ctx.promise->finish();
}

inline QString cfgString(const QString &providerName, const QString &field)
{
    return ConfigManager::instance()
        .providerConfig(providerName)
        .value(field)
        .toString()
        .trimmed();
}

} // namespace provider
