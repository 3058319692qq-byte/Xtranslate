#include "ui/overlay/ControlBar.h"

#include "ui/theme/ThemeManager.h"

#ifdef _WIN32
#  include "ui/platform/WinBackdrop.h"
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

#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QSize>

namespace {

constexpr int kGapAboveRegion = 6; // px between the bar and the region

// Phase 7: 图标按钮工厂——objectName=iconButton 走 qss 模板的玻璃胶囊样式。
// accessibleName 保留原中文文案给 UIA / 屏幕阅读器（红线：可达性名字不删）。
// ControlBar 自身无 objectName（既有契约），按钮统一 iconButton 名字。
QPushButton *makeIconButton(const QString &iconRes, const QString &accessibleName,
                            QWidget *parent)
{
    auto *btn = new QPushButton(parent);
    btn->setObjectName(QStringLiteral("iconButton"));
    btn->setIcon(QIcon(iconRes));
    btn->setIconSize(QSize(18, 18));
    btn->setAccessibleName(accessibleName);
    btn->setToolTip(accessibleName);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setFixedSize(34, 28);
    return btn;
}

} // namespace

ControlBar::ControlBar(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                          | Qt::Tool | Qt::WindowDoesNotAcceptFocus)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(6);

    // Phase 7: 图标化按钮（retranslate/copy/toggle/close）。
    // m_toggleButton 仍指向切换按钮（既有成员不变）；按钮文字走 accessibleName。
    // Phase 7-fix2b：lock/unlock 按钮已移除（覆盖层恒穿透，无双态可切换）。
    // grip 握把放在最左侧，是唯一拖动入口，始终可用。objectName=gripHandle
    // 走 qss 样式；accessibleName="拖动"给 UIA 屏幕阅读器（红线：可达性名字保留）。
    m_grip = new QLabel(QStringLiteral("⠿"), this);
    m_grip->setObjectName(QStringLiteral("gripHandle"));
    m_grip->setAccessibleName(tr("拖动"));
    m_grip->setToolTip(tr("按住拖动覆盖层"));
    m_grip->setCursor(Qt::SizeAllCursor);
    m_grip->setFixedSize(18, 28);
    m_grip->setAlignment(Qt::AlignCenter);
    m_grip->setTextInteractionFlags(Qt::NoTextInteraction);
    m_grip->installEventFilter(this);

    auto *retranslate = makeIconButton(QStringLiteral(":/icons/retranslate.svg"),
                                       tr("重译"), this);
    auto *copy = makeIconButton(QStringLiteral(":/icons/copy.svg"),
                                tr("复制译文"), this);
    m_toggleButton = makeIconButton(QStringLiteral(":/icons/toggle.svg"),
                                    tr("隐藏译文"), this);
    auto *close = makeIconButton(QStringLiteral(":/icons/close.svg"),
                                 tr("关闭"), this);

    layout->addWidget(m_grip);
    layout->addWidget(retranslate);
    layout->addWidget(copy);
    layout->addWidget(m_toggleButton);
    layout->addWidget(close);

    connect(retranslate, &QPushButton::clicked,
            this, &ControlBar::retranslateRequested);
    connect(copy, &QPushButton::clicked, this, &ControlBar::copyRequested);
    connect(m_toggleButton, &QPushButton::clicked,
            this, &ControlBar::toggleOverlayRequested);
    connect(close, &QPushButton::clicked, this, &ControlBar::closeRequested);

    // Phase 7: 主题/减少透明度切换时重新应用 backdrop。
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &ControlBar::applyBackdrop);
    connect(&ThemeManager::instance(), &ThemeManager::reduceTransparencyChanged,
            this, &ControlBar::applyBackdrop);

    // Button styling comes from the theme QSS ("ControlBar QPushButton");
    // the rounded background is painted with the palette barBg color.
    adjustSize();
}

void ControlBar::dockTo(const QRect &regionLogical)
{
    adjustSize();
    // Preferred spot: right-aligned just above the region's top-right corner.
    QPoint pos(regionLogical.right() - width() + 1,
               regionLogical.top() - height() - kGapAboveRegion);

    const QScreen *screen = QGuiApplication::screenAt(regionLogical.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect avail = screen->availableGeometry();
    if (pos.y() < avail.top()) // no room above -> tuck inside the top edge
        pos.setY(regionLogical.top() + kGapAboveRegion);
    pos.setX(qBound(avail.left(), pos.x(), avail.right() - width() + 1));

    move(pos);
}

void ControlBar::setOverlayVisible(bool visible)
{
    // Phase 7: 文字按钮已图标化，文字走 accessibleName 与 tooltip。
    // 通过 setToolTip 反映当前状态（隐藏/显示），UIA 仍可读到正确语义。
    m_toggleButton->setToolTip(visible ? tr("隐藏译文") : tr("显示译文"));
    m_toggleButton->setAccessibleName(visible ? tr("隐藏译文") : tr("显示译文"));
}

bool ControlBar::eventFilter(QObject *watched, QEvent *event)
{
    // Phase 7-fix2b：grip 握把是唯一拖动入口。press 记录起点并发 dragStarted()
    // （控制器据此记录 overlay 起始位置）；move 发 dragRequested(累计 delta)，
    // 本窗口用绝对定位 m_gripStartPos + delta，控制器同样用"起点+delta"；
    // release 发 dragFinished()（控制器触发 overlay update() 确保面板可见）。
    // 不影响按钮事件（按钮有自己的 mousePressEvent，不经过此过滤器）。
    if (watched == m_grip) {
        const auto type = event->type();
        if (type == QEvent::MouseButtonPress) {
            const auto *me = static_cast<const QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_gripDragging = true;
                m_gripStartGlobal = me->globalPosition().toPoint();
                m_gripStartPos = pos();
                qInfo().noquote() << QStringLiteral("[controlbar] grip drag start at %1,%2 window pos %3,%4")
                                        .arg(m_gripStartGlobal.x()).arg(m_gripStartGlobal.y())
                                        .arg(m_gripStartPos.x()).arg(m_gripStartPos.y());
                emit dragStarted();
                return true; // 事件已处理，阻止 QLabel 默认行为
            }
        } else if (type == QEvent::MouseMove && m_gripDragging) {
            const auto *me = static_cast<const QMouseEvent *>(event);
            const QPoint delta = me->globalPosition().toPoint() - m_gripStartGlobal;
            move(m_gripStartPos + delta);
            emit dragRequested(delta);
            return true;
        } else if (type == QEvent::MouseButtonRelease && m_gripDragging) {
            const auto *me = static_cast<const QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_gripDragging = false;
                qInfo().noquote() << QStringLiteral("[controlbar] grip drag end");
                emit dragFinished();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ControlBar::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
#ifdef _WIN32
    // Clickable but never activates (keeps the game in the foreground).
    HWND hwnd = reinterpret_cast<HWND>(winId());
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    ex |= WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
#endif
    // Phase 7: HWND 已创建并设置好 NOACTIVATE 后再应用 Acrylic。
    applyBackdrop();
}

void ControlBar::applyBackdrop()
{
#ifdef _WIN32
    QWindow *win = windowHandle();
    if (!win) {
        m_glassActive = false;
        return;
    }
    const bool ok = WinBackdrop::applyAcrylicPopup(win);
    m_glassActive = ok;
    // 触发重绘以反映玻璃/假玻璃底色切换。
    update();
#else
    m_glassActive = false;
#endif
}

void ControlBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()), 8, 8);
    // Phase 7: 真玻璃模式用 glassSurface（半透明）让 Acrylic 模糊透过；
    // 假玻璃/减少透明度时用 barBg（不透明）保持原观感。
    const ThemePalette &pal = ThemeManager::instance().palette();
    const QColor &bg = m_glassActive ? pal.glassSurface : pal.barBg;
    p.fillPath(path, bg);
}
