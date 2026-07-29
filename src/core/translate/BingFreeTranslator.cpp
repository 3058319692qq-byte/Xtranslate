#include "core/translate/BingFreeTranslator.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace {

// Phase 6-tuning：8s → 3s，与 ProviderCommon::kTimeoutMs 保持一致。
constexpr int    kTimeoutMs   = 3000;
constexpr qint64 kTokenTtlMs  = 8 * 60 * 1000;

const QString kAuthUrl = QStringLiteral("https://edge.microsoft.com/translate/auth");
const QString kApiUrl  = QStringLiteral(
    "https://api-edge.cognitive.microsofttranslator.com/translate");

// Bing uses zh-Hans/zh-Hant instead of zh-CN/zh-TW.
QString mapLang(const QString &bcp47)
{
    if (bcp47 == QLatin1String("zh-CN")) return QStringLiteral("zh-Hans");
    if (bcp47 == QLatin1String("zh-TW")) return QStringLiteral("zh-Hant");
    return bcp47;
}

} // namespace

BingFreeTranslator::BingFreeTranslator(QNetworkAccessManager *nam)
    : m_nam(nam)
{
}

QFuture<TransResult> BingFreeTranslator::translate(const QString &text,
                                                   const QString &from,
                                                   const QString &to)
{
    auto promise = std::make_shared<QPromise<TransResult>>();
    promise->start();
    QFuture<TransResult> future = promise->future();

    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();

    if (!m_token.isEmpty() && m_tokenAge.isValid()
        && m_tokenAge.elapsed() < kTokenTtlMs) {
        requestWithToken(m_token, text, from, to, promise, timer);
        return future;
    }

    QNetworkRequest authReq{QUrl(kAuthUrl)};
    authReq.setTransferTimeout(kTimeoutMs);
    authReq.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64)"));

    QNetworkReply *authReply = m_nam->get(authReq);
    QObject::connect(authReply, &QNetworkReply::finished, authReply,
                     [this, promise, timer, authReply, text, from, to]() {
        if (authReply->error() != QNetworkReply::NoError) {
            TransResult result;
            result.provider = QStringLiteral("bing");
            result.elapsedMs = timer->elapsed();
            result.error = QStringLiteral("bing auth: %1").arg(authReply->errorString());
            authReply->deleteLater();
            promise->addResult(result);
            promise->finish();
            return;
        }
        const QString token = QString::fromUtf8(authReply->readAll()).trimmed();
        authReply->deleteLater();
        if (token.isEmpty()) {
            TransResult result;
            result.provider = QStringLiteral("bing");
            result.elapsedMs = timer->elapsed();
            result.error = QStringLiteral("bing auth: empty token");
            promise->addResult(result);
            promise->finish();
            return;
        }
        m_token = token;
        m_tokenAge.restart();
        requestWithToken(token, text, from, to, promise, timer);
    });

    return future;
}

void BingFreeTranslator::requestWithToken(const QString &token, const QString &text,
                                          const QString &from, const QString &to,
                                          std::shared_ptr<QPromise<TransResult>> promise,
                                          std::shared_ptr<QElapsedTimer> timer)
{
    QUrl url(kApiUrl);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("api-version"), QStringLiteral("3.0"));
    if (!from.isEmpty() && from != QLatin1String("auto"))
        query.addQueryItem(QStringLiteral("from"), mapLang(from));
    query.addQueryItem(QStringLiteral("to"), mapLang(to));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setTransferTimeout(kTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("Authorization", ("Bearer " + token).toUtf8());

    QJsonObject item;
    item.insert(QStringLiteral("Text"), text);
    const QByteArray body = QJsonDocument(QJsonArray{item}).toJson(QJsonDocument::Compact);

    QNetworkReply *reply = m_nam->post(request, body);
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [promise, timer, reply]() {
        TransResult result;
        result.provider = QStringLiteral("bing");
        result.elapsedMs = timer->elapsed();

        if (reply->error() != QNetworkReply::NoError) {
            result.error = QStringLiteral("bing: %1").arg(reply->errorString());
        } else {
            const QByteArray payload = reply->readAll();
            const QJsonDocument doc = QJsonDocument::fromJson(payload);
            // [{"detectedLanguage":..,"translations":[{"text":"..","to":".."}]}]
            const QJsonArray translations =
                doc.array().at(0).toObject().value(QStringLiteral("translations")).toArray();
            QString out;
            for (const QJsonValue &t : translations)
                out += t.toObject().value(QStringLiteral("text")).toString();
            if (out.isEmpty())
                result.error = QStringLiteral("bing: unexpected response (%1)")
                                   .arg(QString::fromUtf8(payload.left(120)));
            else
                result.text = out;
        }
        reply->deleteLater();
        promise->addResult(result);
        promise->finish();
    });
}
