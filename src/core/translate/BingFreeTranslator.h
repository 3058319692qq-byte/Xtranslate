// BingFreeTranslator - free Edge-browser translation endpoint.
//
//   1. GET  https://edge.microsoft.com/translate/auth          -> plain JWT
//      (cached for 8 minutes)
//   2. POST https://api-edge.cognitive.microsofttranslator.com/translate
//           ?api-version=3.0[&from={from}]&to={to}
//      body: [{"Text":"..."}]   headers: Authorization: Bearer <jwt>,
//                                        Content-Type: application/json
//
// zh-CN / zh-TW are mapped to zh-Hans / zh-Hant; from=auto omits &from=.

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
    void requestWithToken(const QString &token, const QString &text,
                          const QString &from, const QString &to,
                          std::shared_ptr<QPromise<TransResult>> promise,
                          std::shared_ptr<QElapsedTimer> timer);

    QNetworkAccessManager *m_nam;
    QString m_token;
    QElapsedTimer m_tokenAge;      // valid while m_tokenAge.elapsed() < 8 min
};
