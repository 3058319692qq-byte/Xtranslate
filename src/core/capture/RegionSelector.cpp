#include "core/capture/RegionSelector.h"

#include "core/capture/ScreenCapturer.h"
#include "ui/theme/ThemeManager.h"

#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>

namespace {

constexpr int kDimAlpha = 89;                  // ~35% black veil
constexpr int kHandleRadius = 6;               // corner handle radius (logical px)
constexpr int kMinSelection = 4;               // ignore tiny accidental drags

} // namespace

// ---------------------------------------------------------------------------
// RegionSelector (one per screen)
// ---------------------------------------------------------------------------

RegionSelector::RegionSelector(QScreen *screen)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                           | Qt::Tool)
    , m_screen(screen)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);

    // Freeze this screen's content as the backdrop: no live flicker and the
    // user selects exactly what will be captured.
    const QImage frame = ScreenCapturer::grabVirtualRect(
        ScreenCapturer::logicalToPhysical(screen->geometry()));
    m_backdrop = QPixmap::fromImage(frame);
    m_backdrop.setDevicePixelRatio(screen->devicePixelRatio());

    setScreen(screen);
    setGeometry(screen->geometry());
}

QRect RegionSelector::selectionRect() const
{
    return QRect(m_origin, m_current).normalized();
}

void RegionSelector::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Darkened frozen screen.
    p.drawPixmap(rect(), m_backdrop);
    p.fillRect(rect(), QColor(0, 0, 0, kDimAlpha));

    if (!m_dragging)
        return;

    const QRect sel = selectionRect();
    if (sel.isEmpty())
        return;

    // Restore full brightness inside the selection.
    const qreal dpr = m_backdrop.devicePixelRatio();
    const QRectF srcPhys(sel.x() * dpr, sel.y() * dpr,
                         sel.width() * dpr, sel.height() * dpr);
    p.drawPixmap(QRectF(sel), m_backdrop, srcPhys);

    // Phase 7-fix1：选框改"1px 黑 + 1px 白"双描边，任何背景都清晰（去橙后无彩色）。
    // contract：选框仍由 selectionRect() 几何定义，绘制样式变更不影响
    // regionSelected 信号负载。
    const ThemePalette &pal = ThemeManager::instance().palette();
    p.setBrush(Qt::NoBrush);
    // 先画 2px 白底，再叠 1px 黑内描边，形成黑白双线
    p.setPen(QPen(Qt::white, 2));
    p.drawRoundedRect(sel, pal.glassRadiusControl, pal.glassRadiusControl);
    p.setPen(QPen(Qt::black, 1));
    p.drawRoundedRect(sel.adjusted(0, 0, -1, -1),
                      pal.glassRadiusControl, pal.glassRadiusControl);

    // Corner handles: 圆形白底黑描边胶囊（双描边同选框），中心点改 accentPressed
    // 深灰取代原橙色点缀，符合"无彩色"原则。
    const QPoint corners[4] = {sel.topLeft(), sel.topRight(),
                               sel.bottomLeft(), sel.bottomRight()};
    for (const QPoint &c : corners) {
        p.setPen(QPen(Qt::black, 1.5));
        p.setBrush(Qt::white);
        p.drawEllipse(c, kHandleRadius, kHandleRadius);
        p.setPen(Qt::NoPen);
        p.setBrush(pal.accentPressed);
        p.drawEllipse(c, 2, 2);
    }

    // Size label in PHYSICAL pixels (what the OCR image will measure).
    const QString label = QStringLiteral("%1 x %2 px")
                              .arg(qRound(sel.width() * dpr))
                              .arg(qRound(sel.height() * dpr));
    QFont f = font();
    f.setPointSize(9);
    p.setFont(f);
    const QFontMetrics fm(f);
    const QSize textSize = fm.size(Qt::TextSingleLine, label);
    QRect labelRect(sel.left(), sel.top() - textSize.height() - 10,
                    textSize.width() + 16, textSize.height() + 8);
    if (labelRect.top() < 0)
        labelRect.moveTop(sel.top() + 4); // keep visible near the screen edge
    // Phase 7-fix1：尺寸标签改黑底白字胶囊（原橙底白字已去橙）
    p.setPen(QPen(Qt::black, 1.0));
    p.setBrush(QColor(0x11, 0x11, 0x11));
    p.drawRoundedRect(labelRect, 6, 6);
    p.setPen(Qt::white);
    p.drawText(labelRect, Qt::AlignCenter, label);
}

void RegionSelector::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_origin = event->pos();
        m_current = m_origin;
        update();
    }
}

void RegionSelector::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        m_current = event->pos();
        update();
    }
}

void RegionSelector::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_dragging)
        return;
    m_dragging = false;
    m_current = event->pos();

    const QRect sel = selectionRect();
    if (sel.width() < kMinSelection || sel.height() < kMinSelection) {
        update(); // too small - treat as a no-op, keep selecting
        return;
    }
    emit regionSelected(QRect(mapToGlobal(sel.topLeft()), sel.size()));
}

void RegionSelector::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        emit selectionCancelled();
        return;
    }
    QWidget::keyPressEvent(event);
}

// ---------------------------------------------------------------------------
// RegionSelectSession
// ---------------------------------------------------------------------------

RegionSelectSession::RegionSelectSession(QWidget *callerToHide, QObject *parent)
    : QObject(parent)
    , m_caller(callerToHide)
{
}

void RegionSelectSession::start()
{
    m_callerWasVisible = m_caller && m_caller->isVisible();
    if (m_callerWasVisible) {
        m_caller->hide();
        // Give the compositor a moment to actually remove the window before
        // the backdrops are grabbed.
        QTimer::singleShot(150, this, &RegionSelectSession::openSelectors);
    } else {
        openSelectors();
    }
}

void RegionSelectSession::openSelectors()
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        auto *selector = new RegionSelector(screen);
        connect(selector, &RegionSelector::regionSelected, this,
                [this](const QRect &r) { finish(true, r); });
        connect(selector, &RegionSelector::selectionCancelled, this,
                [this]() { finish(false, QRect()); });
        m_selectors.append(selector);
        // Geometry already covers the screen; StaysOnTop keeps it above the
        // taskbar. (showFullScreen on Qt::Tool windows is unreliable.)
        selector->show();
    }
    // Route Esc to the selector under the mouse.
    for (RegionSelector *selector : m_selectors) {
        if (selector->geometry().contains(QCursor::pos())) {
            selector->activateWindow();
            selector->raise();
            break;
        }
    }
}

void RegionSelectSession::finish(bool selected, const QRect &globalLogicalRect)
{
    if (m_finished)
        return; // a second signal (e.g. Esc racing a release) is ignored
    m_finished = true;

    for (RegionSelector *selector : m_selectors)
        selector->close(); // WA_DeleteOnClose
    m_selectors.clear();

    // Let the compositor actually remove the dark masks before the receiver
    // grabs the region, then restore the caller AFTER the (synchronous) grab
    // inside the signal handler so it cannot leak into the capture.
    QTimer::singleShot(120, this, [this, selected, globalLogicalRect]() {
        if (selected)
            emit regionSelected(globalLogicalRect);
        else
            emit cancelled();
        if (m_callerWasVisible && m_caller)
            m_caller->show();
        deleteLater();
    });
}
