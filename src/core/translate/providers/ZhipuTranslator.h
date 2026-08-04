// ZhipuTranslator - 智谱 AI GLM-4-Flash (OpenAI-compatible, 永久免费模型).
//
// baseUrl 默认 https://open.bigmodel.cn/api/paas/v4（国内直连），
// model 默认 glm-4-flash，仅需 apiKey。baseUrl/model 可覆盖。
// 2026-08：免费接口大清洗后新增——LLM 翻译质量碾压免费机翻，
// 免翻墙、不限量，是 bing/mymemory 之外的高质量兜底源。

#pragma once

#include "core/translate/Translator.h"

class QNetworkAccessManager;

class ZhipuTranslator : public Translator
{
public:
    explicit ZhipuTranslator(QNetworkAccessManager *nam);

    QString name() const override { return QStringLiteral("zhipu"); }
    bool requiresKey() const override { return true; }
    bool isConfigured() const override;

    QFuture<TransResult> translate(const QString &text, const QString &from,
                                   const QString &to) override;

private:
    QNetworkAccessManager *m_nam;
};
