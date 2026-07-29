#include "ui/overlay/OverlayWindow.h"

#include "ui/theme/ThemeManager.h"

#ifdef _WIN32
#  ifndef WINVER
#    define WINVER 0x0A00
#  endif
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0A00
#  endif
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QShowEvent>

namespace {

constexpr qreal kPadX = 8.0;             // panel horizontal padding
constexpr qreal kPadY = 4.0;             // panel vertical padding
constexpr qreal kCornerRadius = 6.0;
constexpr qreal kMinPointSize = 9.0;     // spec: font floor 9pt
constexpr qreal kMaxGrowFactor = 1.8;    // panel may grow to 1.8x group height
// Panel/text colors come from ThemeManager::palette() at paint time so the
// overlay follows light/dark switches without a rebuild.

// Largest point size (>= kMinPointSize) whose word-wrapped bounding box fits
// `width` x `maxHeight`; also reports the resulting text height.
qreal fitPointSize(const QString &text, const QFont &baseFont,
                   qreal startPt, qreal width, qreal maxHeight,
                   qreal *textHeightOut)
{
    QFont font = baseFont;
    qreal pt = qMax(startPt, kMinPointSize);
    qreal height = 0.0;
    while (true) {
        font.setPointSizeF(pt);
        const QFontMetricsF fm(font);
        const QRectF bounds = fm.boundingRect(
            QRectF(0, 0, width, 100000.0),
            Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, text);
        height = bounds.height();
        if (height <= maxHeight || pt <= kMinPointSize)
            break;
        pt -= 1.0;
        if (pt < kMinPointSize)
            pt = kMinPointSize;
    }
    if (textHeightOut)
        *textHeightOut = height;
    return pt;
}

} // namespace

OverlayWindow::OverlayWindow(const QRect &regionLogical, QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                          | Qt::Tool)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    setGeometry(regionLogical);
}

void OverlayWindow::setGroups(const QVector<OverlayGroup> &groups)
{
    m_groups = groups;
    update();
}

void OverlayWindow::setGroupTranslation(int index, const QString &text)
{
    if (index < 0 || index >= m_groups.size())
        return;
    m_groups[index].translatedText = text;
    m_groups[index].loading = false;
    update();
}

QString OverlayWindow::allTranslatedText() const
{
    QStringList parts;
    for (const OverlayGroup &g : m_groups) {
        if (!g.loading && !g.translatedText.isEmpty())
            parts.append(g.translatedText);
    }
    return parts.join(QStringLiteral("\n\n"));
}

void OverlayWindow::pinRegion(const QRect &regionLogical)
{
    // Phase 3: remember the region and re-capture/re-translate on a timer so
    // the overlay follows changing game text. No-op in phase 2 by design.
    Q_UNUSED(regionLogical);
}

void OverlayWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
#ifdef _WIN32
    // Click-through + no-activate, same read-modify-write as ControlBar.
    // WA_TranslucentBackground makes Qt manage WS_EX_LAYERED itself and push
    // frames via UpdateLayeredWindowIndirect (per-pixel alpha). Never call
    // SetLayeredWindowAttributes here: attribute-based layering is mutually
    // exclusive with the per-pixel path and would make every Qt frame fail.
    //
    // Phase 7-fix2b：覆盖层恒穿透——WS_EX_TRANSPARENT 无条件常驻（原
    // setClickThrough 双态已移除，移动统一走 ControlBar grip 拖动）。
    HWND hwnd = reinterpret_cast<HWND>(winId());
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    ex |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
#endif
}

void OverlayWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const ThemePalette &pal = ThemeManager::instance().palette();

    for (const OverlayGroup &g : m_groups) {
        const QString text = g.loading ? g.sourceText : g.translatedText;
        if (text.isEmpty())
            continue;

        // Initial size from the OCR line height (logical px -> pt at 96 dpi),
        // then shrink until the wrapped text fits the group box (which may
        // grow downwards up to kMaxGrowFactor of the original height).
        const qreal startPt = qMax(g.lineHeight * 72.0 / 96.0 * 0.75,
                                   kMinPointSize);
        const qreal textWidth = qMax(g.rect.width() - 2.0 * kPadX, 24.0);
        const qreal maxTextHeight =
            qMax(g.rect.height() * kMaxGrowFactor - 2.0 * kPadY, 12.0);

        qreal textHeight = 0.0;
        QFont font = this->font();
        const qreal pt = fitPointSize(text, font, startPt, textWidth,
                                      maxTextHeight, &textHeight);
        font.setPointSizeF(pt);

        const qreal panelHeight =
            qMin(qMax(textHeight + 2.0 * kPadY, g.rect.height()),
                 g.rect.height() * kMaxGrowFactor);
        QRectF panel(g.rect.x(), g.rect.y(), g.rect.width(), panelHeight);
        // Keep the panel inside the overlay when it grew past the bottom.
        if (panel.bottom() > height())
            panel.moveBottom(height());

        QPainterPath path;
        path.addRoundedRect(panel, kCornerRadius, kCornerRadius);
        // Phase 7: 假玻璃改色——填充仍用 panelBg（始终深色，保证白字可读 +
        // 不破坏 overlay selftest 的 #202020±40 像素断言），叠加 1px glassBorder
        // 描边作为玻璃边缘高光。契约零改动：仍是 WA_TranslucentBackground +
        // UpdateLayeredWindowIndirect 逐像素分层，WS_EX_TRANSPARENT 点击穿透。
        p.fillPath(path, pal.panelBg);
        p.setPen(QPen(pal.glassBorder, 1.0));
        p.drawPath(path);

        p.setFont(font);
        p.setPen(g.loading ? pal.panelLoading : pal.panelText);
        const QRectF textRect = panel.adjusted(kPadX, kPadY, -kPadX, -kPadY);
        p.drawText(textRect,
                   Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignVCenter, text);
    }
}
