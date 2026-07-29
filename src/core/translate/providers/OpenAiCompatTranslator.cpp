#include "core/translate/providers/OpenAiCompatTranslator.h"

#include "core/translate/providers/ProviderCommon.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>

OpenAiCompatTranslator::OpenAiCompatTranslator(QNetworkAccessManager *nam)
    : m_nam(nam)
{
}

bool OpenAiCompatTranslator::isConfigured() const
{
    return !provider::cfgString(name(), QStringLiteral("apiKey")).isEmpty()
        && !provider::cfgString(name(), QStringLiteral("baseUrl")).isEmpty()
        && !provider::cfgString(name(), QStringLiteral("model")).isEmpty();
}

QString OpenAiCompatTranslator::endpointFor(const QString &baseUrl)
{
    QString base = baseUrl.trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    if (base.endsWith(QLatin1String("/chat/completions")))
        return base;
    return base + QStringLiteral("/chat/completions");
}

QString OpenAiCompatTranslator::langName(const QString &bcp47)
{
    if (bcp47 == QLatin1String("auto"))  return QStringLiteral("the detected language");
    if (bcp47 == QLatin1String("zh-CN")) return QStringLiteral("Simplified Chinese");
    if (bcp47 == QLatin1String("zh-TW")) return QStringLiteral("Traditional Chinese");
    if (bcp47 == QLatin1String("en"))    return QStringLiteral("English");
    if (bcp47 == QLatin1String("ja"))    return QStringLiteral("Japanese");
    if (bcp47 == QLatin1String("ko"))    return QStringLiteral("Korean");
    if (bcp47 == QLatin1String("fr"))    return QStringLiteral("French");
    if (bcp47 == QLatin1String("de"))    return QStringLiteral("German");
    if (bcp47 == QLatin1String("es"))    return QStringLiteral("Spanish");
    if (bcp47 == QLatin1String("ru"))    return QStringLiteral("Russian");
    return bcp47;
}

QFuture<TransResult> OpenAiCompatTranslator::translate(const QString &text,
                                                       const QString &from,
                                                       const QString &to)
{
    auto ctx = provider::makeCtx(name());
    QFuture<TransResult> future = ctx.promise->future();

    const QString baseUrl = provider::cfgString(name(), QStringLiteral("baseUrl"));
    const QString apiKey = provider::cfgString(name(), QStringLiteral("apiKey"));
    const QString model = provider::cfgString(name(), QStringLiteral("model"));
    if (baseUrl.isEmpty() || apiKey.isEmpty() || model.isEmpty()) {
        provider::finishError(
            ctx, QStringLiteral("baseUrl/apiKey/model not configured"));
        return future;
    }

    QNetworkRequest request{QUrl(endpointFor(baseUrl))};
    request.setTransferTimeout(provider::kTimeoutMs * 2); // LLMs are slower
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());

    const QString systemPrompt = QStringLiteral(
        "You are a translator. Translate from %1 to %2. "
        "Output only the translation.")
        .arg(langName(from.isEmpty() ? QStringLiteral("auto") : from),
             langName(to));

    QJsonObject sysMsg{{QStringLiteral("role"), QStringLiteral("system")},
                       {QStringLiteral("content"), systemPrompt}};
    QJsonObject userMsg{{QStringLiteral("role"), QStringLiteral("user")},
                        {QStringLiteral("content"), text}};
    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("messages"), QJsonArray{sysMsg, userMsg});
    body.insert(QStringLiteral("temperature"), 0.2);
    body.insert(QStringLiteral("stream"), false);

    QNetworkReply *reply = m_nam->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, reply, [ctx, reply]() {
        const QByteArray payload = reply->readAll();
        const QJsonObject obj = QJsonDocument::fromJson(payload).object();
        if (obj.contains(QStringLiteral("error"))) {
            provider::finishError(ctx, obj.value(QStringLiteral("error"))
                .toObject().value(QStringLiteral("message")).toString());
        } else if (reply->error() != QNetworkReply::NoError) {
            provider::finishError(ctx, reply->errorString());
        } else {
            // {"choices":[{"message":{"content":"..."}}]}
            const QString out = obj.value(QStringLiteral("choices")).toArray()
                .at(0).toObject().value(QStringLiteral("message")).toObject()
                .value(QStringLiteral("content")).toString().trimmed();
            provider::finishText(ctx, out);
        }
        reply->deleteLater();
    });
    return future;
}
