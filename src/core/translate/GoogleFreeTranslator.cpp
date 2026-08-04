#include "core/translate/GoogleFreeTranslator.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPromise>
#include <QUrl>
#include <QUrlQuery>

#include <memory>

namespace {
// Phase 6-tuning：8s → 3s，与 ProviderCommon::kTimeoutMs 保持一致。
constexpr int kTimeoutMs = 3000;
} // namespace

GoogleFreeTranslator::GoogleFreeTranslator(QNetworkAccessManager *nam)
    : m_nam(nam)
{
}

QFuture<TransResult> GoogleFreeTranslator::translate(const QString &text,
                                                     const QString &from,
                                                     const QString &to)
{
    auto promise = std::make_shared<QPromise<TransResult>>();
    promise->start();
    QFuture<TransResult> future = promise->future();

    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();

    QUrl url(QStringLiteral("https://translate.googleapis.com/translate_a/single"));
    // q is percent-encoded explicitly; the query string is set in StrictMode.
    const QString queryString =
        QStringLiteral("client=gtx&sl=%1&tl=%2&dt=t&q=%3")
            .arg(from.isEmpty() ? QStringLiteral("auto") : from, to,
                 QString::fromLatin1(QUrl::toPercentEncoding(text)));
    url.setQuery(queryString, QUrl::StrictMode);

    QNetworkRequest request(url);
    request.setTransferTimeout(kTimeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64)"));

    QNetworkReply *reply = m_nam->get(request);
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [promise, timer, reply]() {
        TransResult result;
        result.provider = QStringLiteral("google");
        result.elapsedMs = timer->elapsed();

        // 2026-08：Google 对 client=gtx 免 token 端点大规模风控，代理/数据
        // 中心 IP 会被 302 到 www.google.com/sorry（验证码页）。识别并给出
        // 清晰错误，避免"network error"这类误导性提示。
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const bool captcha =
            (status >= 300 && status < 400)
            && reply->rawHeader("Location").contains("/sorry/");
        if (captcha || body.contains("sorry/index")) {
            result.error = QStringLiteral(
                "google: blocked by Google anti-bot (captcha redirect). "
                "Free gtx endpoint rejects proxy/VPN IPs; use residential "
                "IP or another provider");
        } else if (reply->error() != QNetworkReply::NoError) {
            result.error = QStringLiteral("google: %1").arg(reply->errorString());
        } else {
            QJsonParseError parseError{};
            const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
                result.error = QStringLiteral("google: unexpected response (%1)")
                                   .arg(QString::fromLatin1(body.left(120)));
            } else {
                // [[["seg1-translated","seg1-source",...],["seg2-...",...]], ...]
                QString out;
                const QJsonArray segments = doc.array().at(0).toArray();
                for (const QJsonValue &seg : segments)
                    out += seg.toArray().at(0).toString();
                if (out.isEmpty())
                    result.error = QStringLiteral("google: empty translation");
                else
                    result.text = out;
            }
        }
        reply->deleteLater();
        promise->addResult(result);
        promise->finish();
    });

    return future;
}
