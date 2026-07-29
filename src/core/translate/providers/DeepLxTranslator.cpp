#include "core/translate/providers/DeepLxTranslator.h"

#include "core/translate/providers/DeepLTranslator.h"
#include "core/translate/providers/ProviderCommon.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>

DeepLxTranslator::DeepLxTranslator(QNetworkAccessManager *nam)
    : m_nam(nam)
{
}

bool DeepLxTranslator::isConfigured() const
{
    return !provider::cfgString(name(), QStringLiteral("baseUrl")).isEmpty();
}

QFuture<TransResult> DeepLxTranslator::translate(const QString &text,
                                                 const QString &from,
                                                 const QString &to)
{
    auto ctx = provider::makeCtx(name());
    QFuture<TransResult> future = ctx.promise->future();

    const QString baseUrl = provider::cfgString(name(), QStringLiteral("baseUrl"));
    if (baseUrl.isEmpty()) {
        provider::finishError(ctx, QStringLiteral("baseUrl not configured"));
        return future;
    }

    QNetworkRequest request{QUrl(baseUrl)};
    request.setTransferTimeout(provider::kTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));

    // DeepLX reuses DeepL's language codes; "auto" is the empty source.
    QJsonObject body;
    body.insert(QStringLiteral("text"), text);
    body.insert(QStringLiteral("source_lang"),
                (from.isEmpty() || from == QLatin1String("auto"))
                    ? QStringLiteral("auto")
                    : DeepLTranslator::mapLang(from, false));
    body.insert(QStringLiteral("target_lang"),
                DeepLTranslator::mapLang(to, false));

    QNetworkReply *reply = m_nam->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, reply, [ctx, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            provider::finishError(ctx, reply->errorString());
        } else {
            const QJsonObject obj =
                QJsonDocument::fromJson(reply->readAll()).object();
            const int code = obj.value(QStringLiteral("code")).toInt();
            if (code != 200) {
                provider::finishError(
                    ctx, QStringLiteral("code %1").arg(code));
            } else {
                provider::finishText(
                    ctx, obj.value(QStringLiteral("data")).toString());
            }
        }
        reply->deleteLater();
    });
    return future;
}
