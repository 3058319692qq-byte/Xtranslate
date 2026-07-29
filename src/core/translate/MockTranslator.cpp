#include "core/translate/MockTranslator.h"

#include <QFuture>

QFuture<TransResult> MockTranslator::translate(const QString &text,
                                               const QString &from,
                                               const QString &to)
{
    Q_UNUSED(from);
    Q_UNUSED(to);
    TransResult result;
    result.text = QStringLiteral("[MOCK] ") + text;
    result.provider = QStringLiteral("mock");
    result.elapsedMs = 0;
    return QtFuture::makeReadyValueFuture(result);
}
