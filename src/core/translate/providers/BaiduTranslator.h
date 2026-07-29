// BaiduTranslator - 百度翻译开放平台 (appid + key, MD5 signature).
//
// POST https://fanyi-api.baidu.com/api/trans/vip/translate with form fields
// q/from/to/appid/salt/sign, where sign = MD5(appid + q + salt + key) over
// the UTF-8 bytes (q NOT url-encoded when signing).

#pragma once

#include "core/translate/Translator.h"

class QNetworkAccessManager;

class BaiduTranslator : public Translator
{
public:
    explicit BaiduTranslator(QNetworkAccessManager *nam);

    QString name() const override { return QStringLiteral("baidu"); }
    bool requiresKey() const override { return true; }
    bool isConfigured() const override;

    QFuture<TransResult> translate(const QString &text, const QString &from,
                                   const QString &to) override;

    // Lowercase MD5 hex of appid+q+salt+key (official doc vector:
    // 2015063000000001/apple/1435660288/12345678 ->
    // f89f9594663708c1605f3d736d01d2d4).
    static QString sign(const QString &appId, const QString &q,
                        const QString &salt, const QString &key);
    // BCP-47 -> Baidu codes (zh / cht / en / jp / kor / fra / de / spa / ru).
    static QString mapLang(const QString &bcp47);

private:
    QNetworkAccessManager *m_nam;
};
