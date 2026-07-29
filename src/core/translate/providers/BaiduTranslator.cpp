#include "core/translate/providers/BaiduTranslator.h"

#include "core/translate/providers/ProviderCommon.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QUrlQuery>

BaiduTranslator::BaiduTranslator(QNetworkAccessManager *nam)
    : m_nam(nam)
{
}

bool BaiduTranslator::isConfigured() const
{
    return !provider::cfgString(name(), QStringLiteral("appId")).isEmpty()
        && !provider::cfgString(name(), QStringLiteral("apiKey")).isEmpty();
}

QString BaiduTranslator::sign(const QString &appId, const QString &q,
                              const QString &salt, const QString &key)
{
    const QByteArray raw = (appId + q + salt + key).toUtf8();
    return QString::fromLatin1(
        QCryptographicHash::hash(raw, QCryptographicHash::Md5).toHex());
}

QString BaiduTranslator::mapLang(const QString &bcp47)
{
    if (bcp47 == QLatin1String("auto"))  return QStringLiteral("auto");
    if (bcp47 == QLatin1String("zh-CN")) return QStringLiteral("zh");
    if (bcp47 == QLatin1String("zh-TW")) return QStringLiteral("cht");
    if (bcp47 == QLatin1String("ja"))    return QStringLiteral("jp");
    if (bcp47 == QLatin1String("ko"))    return QStringLiteral("kor");
    if (bcp47 == QLatin1String("fr"))    return QStringLiteral("fra");
    if (bcp47 == QLatin1String("es"))    return QStringLiteral("spa");
    return bcp47; // en / de / ru already match
}

QFuture<TransResult> BaiduTranslator::translate(const QString &text,
                                                const QString &from,
                                                const QString &to)
{
    auto ctx = provider::makeCtx(name());
    QFuture<TransResult> future = ctx.promise->future();

    const QString appId = provider::cfgString(name(), QStringLiteral("appId"));
    const QString key = provider::cfgString(name(), QStringLiteral("apiKey"));
    if (appId.isEmpty() || key.isEmpty()) {
        provider::finishError(ctx, QStringLiteral("appid/key not configured"));
        return future;
    }

    const QString salt =
        QString::number(QRandomGenerator::global()->generate());

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("q"),
                      QString::fromLatin1(QUrl::toPercentEncoding(text)));
    form.addQueryItem(QStringLiteral("from"), mapLang(from.isEmpty()
                          ? QStringLiteral("auto") : from));
    form.addQueryItem(QStringLiteral("to"), mapLang(to));
    form.addQueryItem(QStringLiteral("appid"), appId);
    form.addQueryItem(QStringLiteral("salt"), salt);
    form.addQueryItem(QStringLiteral("sign"), sign(appId, text, salt, key));

    QNetworkRequest request(
        QUrl(QStringLiteral("https://fanyi-api.baidu.com/api/trans/vip/translate")));
    request.setTransferTimeout(provider::kTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));

    QNetworkReply *reply =
        m_nam->post(request, form.toString(QUrl::FullyEncoded).toUtf8());
    QObject::connect(reply, &QNetworkReply::finished, reply, [ctx, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            provider::finishError(ctx, reply->errorString());
        } else {
            const QJsonObject obj =
                QJsonDocument::fromJson(reply->readAll()).object();
            if (obj.contains(QStringLiteral("error_code"))) {
                provider::finishError(ctx, QStringLiteral("%1 %2").arg(
                    obj.value(QStringLiteral("error_code")).toString(),
                    obj.value(QStringLiteral("error_msg")).toString()));
            } else {
                // {"from":..,"to":..,"trans_result":[{"src":..,"dst":..},..]}
                QStringList parts;
                const QJsonArray arr =
                    obj.value(QStringLiteral("trans_result")).toArray();
                for (const QJsonValue &v : arr)
                    parts << v.toObject().value(QStringLiteral("dst")).toString();
                provider::finishText(ctx, parts.join(QLatin1Char('\n')));
            }
        }
        reply->deleteLater();
    });
    return future;
}
