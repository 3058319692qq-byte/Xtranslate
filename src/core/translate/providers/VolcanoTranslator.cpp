#include "core/translate/providers/VolcanoTranslator.h"

#include "core/translate/providers/ProviderCommon.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>

namespace {
const QString kApiUrl =
    QStringLiteral("https://translate.volcengine.com/crx/translate/v1/");
}

VolcanoTranslator::VolcanoTranslator(QNetworkAccessManager *nam)
    : m_nam(nam)
{
}

QString VolcanoTranslator::mapLang(const QString &bcp47)
{
    // zh-CN / zh-Hans 均映射为 zh：服务端对 zh-CN/zh-Hans 会原样返回源文。
    if (bcp47 == QLatin1String("zh-CN")) return QStringLiteral("zh");
    if (bcp47 == QLatin1String("zh-Hans")) return QStringLiteral("zh");
    if (bcp47 == QLatin1String("zh-TW")) return QStringLiteral("zh-Hant");
    if (bcp47 == QLatin1String("zh-Hant")) return QStringLiteral("zh-Hant");
    return bcp47; // auto / en / ja / ko / fr / de / es / ru unchanged
}

QFuture<TransResult> VolcanoTranslator::translate(const QString &text,
                                                  const QString &from,
                                                  const QString &to)
{
    auto ctx = provider::makeCtx(name());
    QFuture<TransResult> future = ctx.promise->future();

    QJsonObject body;
    body.insert(QStringLiteral("text"), text);
    body.insert(QStringLiteral("source_language"),
                mapLang(from.isEmpty() ? QStringLiteral("auto") : from));
    body.insert(QStringLiteral("target_language"), mapLang(to));

    QNetworkRequest request{QUrl(kApiUrl)};
    request.setTransferTimeout(provider::kTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64)"));

    QNetworkReply *reply =
        m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, reply, [ctx, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            provider::finishError(ctx, reply->errorString());
        } else {
            const QJsonObject obj =
                QJsonDocument::fromJson(reply->readAll()).object();
            const QJsonObject resp =
                obj.value(QStringLiteral("base_resp")).toObject();
            const int code = resp.value(QStringLiteral("status_code")).toInt(-1);
            if (code != 0) {
                provider::finishError(
                    ctx, QStringLiteral("status %1 %2")
                             .arg(code)
                             .arg(resp.value(QStringLiteral("status_message"))
                                      .toString()));
            } else {
                provider::finishText(
                    ctx, obj.value(QStringLiteral("translation")).toString());
            }
        }
        reply->deleteLater();
    });
    return future;
}
