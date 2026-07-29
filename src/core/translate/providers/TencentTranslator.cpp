#include "core/translate/providers/TencentTranslator.h"

#include "core/translate/providers/ProviderCommon.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QTimeZone>

namespace {

const QString kHost = QStringLiteral("tmt.tencentcloudapi.com");
const QString kService = QStringLiteral("tmt");
const QString kAction = QStringLiteral("TextTranslate");
const QString kVersion = QStringLiteral("2018-03-21");
const QString kContentType = QStringLiteral("application/json; charset=utf-8");

} // namespace

TencentTranslator::TencentTranslator(QNetworkAccessManager *nam)
    : m_nam(nam)
{
}

bool TencentTranslator::isConfigured() const
{
    return !provider::cfgString(name(), QStringLiteral("secretId")).isEmpty()
        && !provider::cfgString(name(), QStringLiteral("secretKey")).isEmpty();
}

QByteArray TencentTranslator::hmacSha256(const QByteArray &key,
                                         const QByteArray &msg)
{
    return QMessageAuthenticationCode::hash(msg, key,
                                            QCryptographicHash::Sha256);
}

QString TencentTranslator::sha256Hex(const QByteArray &data)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QString TencentTranslator::canonicalRequest(const QByteArray &payload)
{
    // POST / <empty query> + canonical headers (content-type/host/x-tc-action,
    // action lowercased per TC3 spec) + hashed payload.
    return QStringLiteral("POST\n/\n\n"
                          "content-type:%1\n"
                          "host:%2\n"
                          "x-tc-action:%3\n\n"
                          "content-type;host;x-tc-action\n%4")
        .arg(kContentType, kHost, kAction.toLower(), sha256Hex(payload));
}

QString TencentTranslator::stringToSign(qint64 timestamp, const QString &date,
                                        const QString &canonicalReq)
{
    return QStringLiteral("TC3-HMAC-SHA256\n%1\n%2/%3/tc3_request\n%4")
        .arg(QString::number(timestamp), date, kService,
             sha256Hex(canonicalReq.toUtf8()));
}

QString TencentTranslator::signatureFor(const QString &secretKey,
                                        const QString &date,
                                        const QString &strToSign)
{
    const QByteArray secretDate =
        hmacSha256("TC3" + secretKey.toUtf8(), date.toUtf8());
    const QByteArray secretService = hmacSha256(secretDate, kService.toUtf8());
    const QByteArray secretSigning =
        hmacSha256(secretService, QByteArrayLiteral("tc3_request"));
    return QString::fromLatin1(
        hmacSha256(secretSigning, strToSign.toUtf8()).toHex());
}

QString TencentTranslator::mapLang(const QString &bcp47)
{
    if (bcp47 == QLatin1String("zh-CN")) return QStringLiteral("zh");
    return bcp47; // auto / zh-TW / en / ja / ko / fr / de / es / ru unchanged
}

QFuture<TransResult> TencentTranslator::translate(const QString &text,
                                                  const QString &from,
                                                  const QString &to)
{
    auto ctx = provider::makeCtx(name());
    QFuture<TransResult> future = ctx.promise->future();

    const QString secretId = provider::cfgString(name(), QStringLiteral("secretId"));
    const QString secretKey = provider::cfgString(name(), QStringLiteral("secretKey"));
    if (secretId.isEmpty() || secretKey.isEmpty()) {
        provider::finishError(ctx,
                              QStringLiteral("SecretId/SecretKey not configured"));
        return future;
    }
    QString region = provider::cfgString(name(), QStringLiteral("region"));
    if (region.isEmpty())
        region = QStringLiteral("ap-guangzhou");

    QJsonObject bodyObj;
    bodyObj.insert(QStringLiteral("SourceText"), text);
    bodyObj.insert(QStringLiteral("Source"), mapLang(from.isEmpty()
                       ? QStringLiteral("auto") : from));
    bodyObj.insert(QStringLiteral("Target"), mapLang(to));
    bodyObj.insert(QStringLiteral("ProjectId"), 0);
    const QByteArray payload =
        QJsonDocument(bodyObj).toJson(QJsonDocument::Compact);

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const qint64 timestamp = now.toSecsSinceEpoch();
    const QString date = now.toString(QStringLiteral("yyyy-MM-dd"));

    const QString signature = signatureFor(
        secretKey, date, stringToSign(timestamp, date, canonicalRequest(payload)));
    const QString authorization = QStringLiteral(
        "TC3-HMAC-SHA256 Credential=%1/%2/%3/tc3_request, "
        "SignedHeaders=content-type;host;x-tc-action, Signature=%4")
        .arg(secretId, date, kService, signature);

    QNetworkRequest request(QUrl(QStringLiteral("https://") + kHost));
    request.setTransferTimeout(provider::kTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader, kContentType);
    request.setRawHeader("Authorization", authorization.toUtf8());
    request.setRawHeader("X-TC-Action", kAction.toUtf8());
    request.setRawHeader("X-TC-Version", kVersion.toUtf8());
    request.setRawHeader("X-TC-Timestamp", QByteArray::number(timestamp));
    request.setRawHeader("X-TC-Region", region.toUtf8());

    QNetworkReply *reply = m_nam->post(request, payload);
    QObject::connect(reply, &QNetworkReply::finished, reply, [ctx, reply]() {
        // Tencent returns HTTP 200 even for API errors; check Response.Error.
        const QJsonObject response = QJsonDocument::fromJson(reply->readAll())
            .object().value(QStringLiteral("Response")).toObject();
        if (reply->error() != QNetworkReply::NoError
            && response.isEmpty()) {
            provider::finishError(ctx, reply->errorString());
        } else if (response.contains(QStringLiteral("Error"))) {
            const QJsonObject err =
                response.value(QStringLiteral("Error")).toObject();
            provider::finishError(ctx, QStringLiteral("%1 %2").arg(
                err.value(QStringLiteral("Code")).toString(),
                err.value(QStringLiteral("Message")).toString()));
        } else {
            provider::finishText(
                ctx, response.value(QStringLiteral("TargetText")).toString());
        }
        reply->deleteLater();
    });
    return future;
}
