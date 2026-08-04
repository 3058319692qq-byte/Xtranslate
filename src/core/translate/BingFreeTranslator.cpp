#include "core/translate/BingFreeTranslator.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace {

// 2026-08：edge.microsoft.com/translate/auth 已下线（404），改用 Bing 网页版
// ttranslatev3 端点。国内直连 cn.bing.com 无需翻墙。
constexpr int kTimeoutMs = 3000;

const QString kPageUrl =
    QStringLiteral("https://cn.bing.com/translator?mkt=zh-CN");
const QString kApiUrl =
    QStringLiteral("https://cn.bing.com/ttranslatev3");
const QString kIid = QStringLiteral("translator.5023");
const QString kUa =
    QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
                   " (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

// Bing uses zh-Hans/zh-Hant instead of zh-CN/zh-TW; source auto -> auto-detect.
QString mapLang(const QString &bcp47, bool source)
{
    if (bcp47 == QLatin1String("zh-CN")) return QStringLiteral("zh-Hans");
    if (bcp47 == QLatin1String("zh-TW")) return QStringLiteral("zh-Hant");
    if (source && (bcp47.isEmpty() || bcp47 == QLatin1String("auto")))
        return QStringLiteral("auto-detect");
    return bcp47;
}

void finishError(const std::shared_ptr<QPromise<TransResult>> &promise,
                 const std::shared_ptr<QElapsedTimer> &timer,
                 const QString &message)
{
    TransResult result;
    result.provider = QStringLiteral("bing");
    result.elapsedMs = timer->elapsed();
    result.error = QStringLiteral("bing: %1").arg(message);
    promise->addResult(result);
    promise->finish();
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

    // 页面 token 缓存有效期内直接复用；401 时会强制刷新重试一次。
    if (!m_token.isEmpty() && m_tokenAge.isValid()
        && m_tokenAge.elapsed() < m_tokenTtlMs) {
        requestWithToken(m_ig, m_key, m_token, text, from, to, promise, timer, 1);
        return future;
    }

    fetchPageAndTranslate(text, from, to, promise, timer, 1);
    return future;
}

void BingFreeTranslator::fetchPageAndTranslate(
    const QString &text, const QString &from, const QString &to,
    std::shared_ptr<QPromise<TransResult>> promise,
    std::shared_ptr<QElapsedTimer> timer, int refreshAttempts)
{
    QNetworkRequest pageReq{QUrl(kPageUrl)};
    pageReq.setTransferTimeout(kTimeoutMs);
    pageReq.setHeader(QNetworkRequest::UserAgentHeader, kUa);
    pageReq.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9");

    QNetworkReply *pageReply = m_nam->get(pageReq);
    QObject::connect(pageReply, &QNetworkReply::finished, pageReply,
                     [this, promise, timer, text, from, to, refreshAttempts,
                      pageReply]() {
        if (pageReply->error() != QNetworkReply::NoError) {
            finishError(promise, timer,
                        QStringLiteral("page: %1").arg(pageReply->errorString()));
            pageReply->deleteLater();
            return;
        }
        const QString html = QString::fromUtf8(pageReply->readAll());
        pageReply->deleteLater();

        const QRegularExpression igRe(QStringLiteral("IG:\"([0-9A-F]{32})\""));
        const QRegularExpression ppRe(
            QStringLiteral("params_AbusePreventionHelper\\s*=\\s*\\[([^\\]]+)\\]"));
        const QRegularExpressionMatch igM = igRe.match(html);
        const QRegularExpressionMatch ppM = ppRe.match(html);
        if (!igM.hasMatch() || !ppM.hasMatch()) {
            finishError(promise, timer,
                        QStringLiteral("page: IG/params_AbusePreventionHelper missing"));
            return;
        }

        // params_AbusePreventionHelper = [key, "token", ttlMs, ...]
        const QStringList parts =
            ppM.captured(1).split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (parts.size() < 2) {
            finishError(promise, timer,
                        QStringLiteral("page: malformed params_AbusePreventionHelper"));
            return;
        }
        QString key = parts.at(0).trimmed();
        key.remove(QLatin1Char('"'));
        QString token = parts.at(1).trimmed();
        token.remove(QLatin1Char('"'));
        qint64 ttlMs = 3600 * 1000; // fallback: 1h
        if (parts.size() >= 3) {
            bool ok = false;
            const qint64 v = parts.at(2).trimmed().toLongLong(&ok);
            if (ok && v > 0)
                ttlMs = v;
        }

        m_ig = igM.captured(1);
        m_key = key;
        m_token = token;
        m_tokenAge.restart();
        m_tokenTtlMs = ttlMs;
        requestWithToken(m_ig, m_key, m_token, text, from, to, promise, timer,
                         refreshAttempts);
    });
}

void BingFreeTranslator::requestWithToken(
    const QString &ig, const QString &key, const QString &token,
    const QString &text, const QString &from, const QString &to,
    std::shared_ptr<QPromise<TransResult>> promise,
    std::shared_ptr<QElapsedTimer> timer, int refreshAttempts)
{
    QUrl url(kApiUrl);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("isVertical"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("IG"), ig);
    query.addQueryItem(QStringLiteral("IID"), kIid);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setTransferTimeout(kTimeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, kUa);
    request.setRawHeader("Origin", "https://cn.bing.com");
    request.setRawHeader("Referer", "https://cn.bing.com/translator");
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("fromLang"), mapLang(from, true));
    form.addQueryItem(QStringLiteral("to"), mapLang(to, false));
    form.addQueryItem(QStringLiteral("text"), text);
    form.addQueryItem(QStringLiteral("token"), token);
    form.addQueryItem(QStringLiteral("key"), key);
    form.addQueryItem(QStringLiteral("tryFetchingGenderDebiasedTranslations"),
                      QStringLiteral("true"));
    const QByteArray body = form.toString(QUrl::FullyEncoded).toUtf8();

    QNetworkReply *reply = m_nam->post(request, body);
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [this, promise, timer, reply, text, from, to,
                      refreshAttempts]() {
        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        // 401 + ShowCaptcha:false = token 失效或触发风控：刷新页面参数重试一次。
        if (httpStatus == 401) {
            reply->deleteLater();
            if (refreshAttempts > 0) {
                m_token.clear();
                fetchPageAndTranslate(text, from, to, promise, timer,
                                     refreshAttempts - 1);
                return;
            }
            finishError(promise, timer, QStringLiteral("unauthorized (401)"));
            return;
        }

        TransResult result;
        result.provider = QStringLiteral("bing");
        result.elapsedMs = timer->elapsed();

        if (reply->error() != QNetworkReply::NoError) {
            result.error = QStringLiteral("bing: %1").arg(reply->errorString());
        } else {
            const QByteArray payload = reply->readAll();
            const QJsonDocument doc = QJsonDocument::fromJson(payload);
            // [{"translations":[{"text":"..","to":".."},...],"detectedLanguage":..}]
            const QJsonArray arr = doc.array();
            const QJsonArray translations =
                arr.isEmpty() ? QJsonArray()
                              : arr.at(0).toObject()
                                    .value(QStringLiteral("translations"))
                                    .toArray();
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
