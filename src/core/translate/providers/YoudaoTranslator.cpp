#include "core/translate/providers/YoudaoTranslator.h"

#include "core/translate/providers/ProviderCommon.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QUuid>

YoudaoTranslator::YoudaoTranslator(QNetworkAccessManager *nam)
    : m_nam(nam)
{
}

bool YoudaoTranslator::isConfigured() const
{
    return !provider::cfgString(name(), QStringLiteral("appKey")).isEmpty()
        && !provider::cfgString(name(), QStringLiteral("appSecret")).isEmpty();
}

QString YoudaoTranslator::truncateInput(const QString &q)
{
    const int len = q.size();
    if (len <= 20)
        return q;
    return q.left(10) + QString::number(len) + q.right(10);
}

QString YoudaoTranslator::sign(const QString &appKey, const QString &input,
                               const QString &salt, const QString &curtime,
                               const QString &appSecret)
{
    const QByteArray raw = (appKey + input + salt + curtime + appSecret).toUtf8();
    return QString::fromLatin1(
        QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex());
}

QString YoudaoTranslator::mapLang(const QString &bcp47)
{
    if (bcp47 == QLatin1String("zh-CN")) return QStringLiteral("zh-CHS");
    if (bcp47 == QLatin1String("zh-TW")) return QStringLiteral("zh-CHT");
    return bcp47; // auto / en / ja / ko / fr / de / es / ru unchanged
}

QFuture<TransResult> YoudaoTranslator::translate(const QString &text,
                                                 const QString &from,
                                                 const QString &to)
{
    auto ctx = provider::makeCtx(name());
    QFuture<TransResult> future = ctx.promise->future();

    const QString appKey = provider::cfgString(name(), QStringLiteral("appKey"));
    const QString secret = provider::cfgString(name(), QStringLiteral("appSecret"));
    if (appKey.isEmpty() || secret.isEmpty()) {
        provider::finishError(ctx,
                              QStringLiteral("appKey/appSecret not configured"));
        return future;
    }

    const QString salt = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString curtime =
        QString::number(QDateTime::currentSecsSinceEpoch());

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("q"),
                      QString::fromLatin1(QUrl::toPercentEncoding(text)));
    form.addQueryItem(QStringLiteral("from"), mapLang(from.isEmpty()
                          ? QStringLiteral("auto") : from));
    form.addQueryItem(QStringLiteral("to"), mapLang(to));
    form.addQueryItem(QStringLiteral("appKey"), appKey);
    form.addQueryItem(QStringLiteral("salt"), salt);
    form.addQueryItem(QStringLiteral("sign"),
                      sign(appKey, truncateInput(text), salt, curtime, secret));
    form.addQueryItem(QStringLiteral("signType"), QStringLiteral("v3"));
    form.addQueryItem(QStringLiteral("curtime"), curtime);

    QNetworkRequest request(QUrl(QStringLiteral("https://openapi.youdao.com/api")));
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
            const QString errorCode =
                obj.value(QStringLiteral("errorCode")).toString();
            if (errorCode != QLatin1String("0")) {
                provider::finishError(
                    ctx, QStringLiteral("errorCode %1").arg(errorCode));
            } else {
                QStringList parts;
                const QJsonArray arr =
                    obj.value(QStringLiteral("translation")).toArray();
                for (const QJsonValue &v : arr)
                    parts << v.toString();
                provider::finishText(ctx, parts.join(QLatin1Char('\n')));
            }
        }
        reply->deleteLater();
    });
    return future;
}
