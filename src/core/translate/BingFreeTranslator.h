// BingFreeTranslator - free Bing web translator endpoint (ttranslatev3).
//
//   1. GET  https://cn.bing.com/translator?mkt=zh-CN -> extract IG and
//      params_AbusePreventionHelper = [key, "token", ttlMs]
//      (cached until ttl expires; CN endpoint reachable without a proxy)
//   2. POST https://cn.bing.com/ttranslatev3?isVertical=1&&IG={IG}&IID=translator.5023
//      form: fromLang / to / text / token / key / tryFetchingGenderDebiasedTranslations
//
// zh-CN / zh-TW are mapped to zh-Hans / zh-Hant; from=auto sends "auto-detect".

#pragma once

#include "core/translate/Translator.h"

#include <QElapsedTimer>
#include <QPromise>

#include <memory>

class QNetworkAccessManager;

class BingFreeTranslator : public Translator
{
public:
    explicit BingFreeTranslator(QNetworkAccessManager *nam);

    QString name() const override { return QStringLiteral("bing"); }
    QFuture<TransResult> translate(const QString &text,
                                   const QString &from,
                                   const QString &to) override;

private:
    void fetchPageAndTranslate(const QString &text, const QString &from,
                               const QString &to,
                               std::shared_ptr<QPromise<TransResult>> promise,
                               std::shared_ptr<QElapsedTimer> timer,
                               int refreshAttempts);
    void requestWithToken(const QString &ig, const QString &key,
                          const QString &token, const QString &text,
                          const QString &from, const QString &to,
                          std::shared_ptr<QPromise<TransResult>> promise,
                          std::shared_ptr<QElapsedTimer> timer,
                          int refreshAttempts);

    QNetworkAccessManager *m_nam;
    QString m_ig;
    QString m_key;
    QString m_token;
    QElapsedTimer m_tokenAge;
    qint64 m_tokenTtlMs = 0; // 0 = not fetched yet
};
