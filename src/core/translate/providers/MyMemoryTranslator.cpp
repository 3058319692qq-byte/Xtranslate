#include "core/translate/providers/MyMemoryTranslator.h"

#include "core/translate/providers/ProviderCommon.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

MyMemoryTranslator::MyMemoryTranslator(QNetworkAccessManager *nam)
    : m_nam(nam)
{
}

QFuture<TransResult> MyMemoryTranslator::translate(const QString &text,
                                                   const QString &from,
                                                   const QString &to)
{
    auto ctx = provider::makeCtx(name());
    QFuture<TransResult> future = ctx.promise->future();

    // MyMemory 不支持自动检测源语言，auto 时快速失败让链路降级到下一家。
    if (from.isEmpty() || from == QLatin1String("auto")) {
        provider::finishError(ctx, QStringLiteral("auto source not supported"));
        return future;
    }

    QUrl url(QStringLiteral("https://api.mymemory.translated.net/get"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), text);
    query.addQueryItem(QStringLiteral("langpair"),
                       QStringLiteral("%1|%2").arg(from, to));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setTransferTimeout(provider::kTimeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64)"));

    QNetworkReply *reply = m_nam->get(request);
    QObject::connect(reply, &QNetworkReply::finished, reply, [ctx, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            provider::finishError(ctx, reply->errorString());
        } else {
            const QJsonObject obj =
                QJsonDocument::fromJson(reply->readAll()).object();
            const int status =
                obj.value(QStringLiteral("responseStatus")).toInt(0);
            if (status != 200) {
                provider::finishError(
                    ctx, QStringLiteral("status %1 %2")
                             .arg(status)
                             .arg(obj.value(QStringLiteral("responseDetails"))
                                      .toString()));
            } else {
                provider::finishText(
                    ctx, obj.value(QStringLiteral("responseData"))
                             .toObject()
                             .value(QStringLiteral("translatedText"))
                             .toString());
            }
        }
        reply->deleteLater();
    });
    return future;
}
