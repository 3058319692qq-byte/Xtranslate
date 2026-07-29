#include "core/translate/providers/DeepLTranslator.h"

#include "core/translate/providers/ProviderCommon.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>

DeepLTranslator::DeepLTranslator(QNetworkAccessManager *nam)
    : m_nam(nam)
{
}

bool DeepLTranslator::isConfigured() const
{
    return !provider::cfgString(name(), QStringLiteral("apiKey")).isEmpty();
}

QString DeepLTranslator::mapLang(const QString &bcp47, bool isTarget)
{
    if (bcp47 == QLatin1String("zh-CN")) return QStringLiteral("ZH");
    if (bcp47 == QLatin1String("zh-TW")) return QStringLiteral("ZH-HANT");
    if (bcp47 == QLatin1String("en"))
        return isTarget ? QStringLiteral("EN-US") : QStringLiteral("EN");
    return bcp47.toUpper(); // JA / KO / FR / DE / ES / RU pass straight through
}

QString DeepLTranslator::hostForKey(const QString &authKey)
{
    return authKey.endsWith(QLatin1String(":fx"))
               ? QStringLiteral("api-free.deepl.com")
               : QStringLiteral("api.deepl.com");
}

QFuture<TransResult> DeepLTranslator::translate(const QString &text,
                                                const QString &from,
                                                const QString &to)
{
    auto ctx = provider::makeCtx(name());
    QFuture<TransResult> future = ctx.promise->future();

    const QString key = provider::cfgString(name(), QStringLiteral("apiKey"));
    if (key.isEmpty()) {
        provider::finishError(ctx, QStringLiteral("auth key not configured"));
        return future;
    }

    QNetworkRequest request(
        QUrl(QStringLiteral("https://%1/v2/translate").arg(hostForKey(key))));
    request.setTransferTimeout(provider::kTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("Authorization",
                         ("DeepL-Auth-Key " + key).toUtf8());

    QJsonObject body;
    body.insert(QStringLiteral("text"), QJsonArray{text});
    body.insert(QStringLiteral("target_lang"), mapLang(to, true));
    if (!from.isEmpty() && from != QLatin1String("auto"))
        body.insert(QStringLiteral("source_lang"), mapLang(from, false));

    QNetworkReply *reply = m_nam->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, reply, [ctx, reply]() {
        const QByteArray payload = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            provider::finishError(ctx, reply->errorString());
        } else {
            // {"translations":[{"detected_source_language":"EN","text":".."}]}
            const QJsonArray translations = QJsonDocument::fromJson(payload)
                .object().value(QStringLiteral("translations")).toArray();
            QString out;
            for (const QJsonValue &t : translations)
                out += t.toObject().value(QStringLiteral("text")).toString();
            provider::finishText(ctx, out);
        }
        reply->deleteLater();
    });
    return future;
}
