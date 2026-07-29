#include "core/translate/providers/LingvaTranslator.h"

#include "core/translate/providers/ProviderCommon.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>

LingvaTranslator::LingvaTranslator(QNetworkAccessManager *nam)
    : m_nam(nam)
{
}

bool LingvaTranslator::isConfigured() const
{
    return !provider::cfgString(name(), QStringLiteral("baseUrl")).isEmpty();
}

QString LingvaTranslator::mapLang(const QString &bcp47)
{
    if (bcp47 == QLatin1String("zh-CN")) return QStringLiteral("zh");
    if (bcp47 == QLatin1String("zh-TW")) return QStringLiteral("zh_HANT");
    return bcp47; // auto / en / ja / ko / fr / de / es / ru unchanged
}

QFuture<TransResult> LingvaTranslator::translate(const QString &text,
                                                 const QString &from,
                                                 const QString &to)
{
    auto ctx = provider::makeCtx(name());
    QFuture<TransResult> future = ctx.promise->future();

    QString baseUrl = provider::cfgString(name(), QStringLiteral("baseUrl"));
    if (baseUrl.isEmpty()) {
        provider::finishError(ctx, QStringLiteral("baseUrl not configured"));
        return future;
    }
    while (baseUrl.endsWith(QLatin1Char('/')))
        baseUrl.chop(1);

    const QString url = QStringLiteral("%1/api/v1/%2/%3/%4")
        .arg(baseUrl,
             mapLang(from.isEmpty() ? QStringLiteral("auto") : from),
             mapLang(to),
             QString::fromLatin1(QUrl::toPercentEncoding(text)));

    QNetworkRequest request{QUrl(url, QUrl::StrictMode)};
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
            provider::finishText(
                ctx, obj.value(QStringLiteral("translation")).toString());
        }
        reply->deleteLater();
    });
    return future;
}
