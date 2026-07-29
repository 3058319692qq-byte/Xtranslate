// TencentTranslator - 腾讯云机器翻译 TMT (SecretId/SecretKey, TC3-HMAC-SHA256).
//
// POST https://tmt.tencentcloudapi.com with X-TC-Action: TextTranslate,
// X-TC-Version: 2018-03-21. The TC3 signature pipeline (canonical request ->
// string-to-sign -> derived signing key) is exposed as static functions so
// the self-test can verify them against known vectors without a real key.

#pragma once

#include "core/translate/Translator.h"

#include <QByteArray>

class QNetworkAccessManager;

class TencentTranslator : public Translator
{
public:
    explicit TencentTranslator(QNetworkAccessManager *nam);

    QString name() const override { return QStringLiteral("tencent"); }
    bool requiresKey() const override { return true; }
    bool isConfigured() const override;

    QFuture<TransResult> translate(const QString &text, const QString &from,
                                   const QString &to) override;

    // ---- TC3 building blocks (testable without credentials) ----
    static QByteArray hmacSha256(const QByteArray &key, const QByteArray &msg);
    static QString sha256Hex(const QByteArray &data);
    static QString canonicalRequest(const QByteArray &payload);
    static QString stringToSign(qint64 timestamp, const QString &date,
                                const QString &canonicalReq);
    static QString signatureFor(const QString &secretKey, const QString &date,
                                const QString &strToSign);

    // BCP-47 -> TMT codes (zh / zh-TW, rest identical).
    static QString mapLang(const QString &bcp47);

private:
    QNetworkAccessManager *m_nam;
};
