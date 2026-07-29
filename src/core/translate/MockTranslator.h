// MockTranslator - always-succeeding offline fallback.
//
// Registered as the last provider in TranslationManager so the whole pipeline
// stays verifiable without internet access. Output: "[MOCK] " + source text.

#pragma once

#include "core/translate/Translator.h"

class MockTranslator : public Translator
{
public:
    QString name() const override { return QStringLiteral("mock"); }
    QFuture<TransResult> translate(const QString &text,
                                   const QString &from,
                                   const QString &to) override;
};
