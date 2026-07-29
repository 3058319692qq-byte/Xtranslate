// GoogleFreeTranslator - free "gtx" endpoint of translate.googleapis.com.
//
//   GET https://translate.googleapis.com/translate_a/single
//       ?client=gtx&sl={from}&tl={to}&dt=t&q={text}
//
// The response is a nested JSON array; element [0] is a list of segments and
// each segment's [0] holds the translated chunk - all chunks are concatenated.

#pragma once

#include "core/translate/Translator.h"

class QNetworkAccessManager;

class GoogleFreeTranslator : public Translator
{
public:
    explicit GoogleFreeTranslator(QNetworkAccessManager *nam);

    QString name() const override { return QStringLiteral("google"); }
    QFuture<TransResult> translate(const QString &text,
                                   const QString &from,
                                   const QString &to) override;

private:
    QNetworkAccessManager *m_nam;
};
