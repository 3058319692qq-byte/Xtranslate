#include "core/translate/providers/ZhipuTranslator.h"

#include "core/translate/providers/OpenAiCompatTranslator.h"
#include "core/translate/providers/ProviderCommon.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>

namespace {
constexpr auto kDefaultBaseUrl = "https://open.bigmodel.cn/api/paas/v4";
constexpr auto kDefaultModel = "glm-4-flash";
} // namespace

ZhipuTranslator::ZhipuTranslator(QNetworkAccessManager *nam)
    : m_nam(nam)
{
}

bool ZhipuTranslator::isConfigured() const
{
    return !provider::cfgString(name(), QStringLiteral("apiKey")).isEmpty();
}

QFuture<TransResult> ZhipuTranslator::translate(const QString &text,
                                                const QString &from,
                                                const QString &to)
{
    auto ctx = provider::makeCtx(name());
    QFuture<TransResult> future = ctx.promise->future();

    const QString apiKey = provider::cfgString(name(), QStringLiteral("apiKey"));
    if (apiKey.isEmpty()) {
        provider::finishError(ctx, QStringLiteral("apiKey not configured"));
        return future;
    }

    QString baseUrl =
        provider::cfgString(name(), QStringLiteral("baseUrl"));
    if (baseUrl.isEmpty())
        baseUrl = QString::fromLatin1(kDefaultBaseUrl);
    QString model = provider::cfgString(name(), QStringLiteral("model"));
    if (model.isEmpty())
        model = QString::fromLatin1(kDefaultModel);

    QNetworkRequest request{
        QUrl(OpenAiCompatTranslator::endpointFor(baseUrl))};
    request.setTransferTimeout(provider::kTimeoutMs * 2); // LLMs are slower
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());

    const QString systemPrompt = QStringLiteral(
        "You are a translator. Translate from %1 to %2. "
        "Output only the translation.")
        .arg(OpenAiCompatTranslator::langName(
                 from.isEmpty() ? QStringLiteral("auto") : from),
             OpenAiCompatTranslator::langName(to));

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
