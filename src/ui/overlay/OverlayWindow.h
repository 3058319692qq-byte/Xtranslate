// OverlayWindow - click-through translated-text overlay.
//
// Frameless, always-on-top, never takes focus, and mouse events ALWAYS pass
// through to whatever is underneath:
//   Qt side  : FramelessWindowHint | WindowStaysOnTopHint | Tool
//              + WA_TranslucentBackground + WA_ShowWithoutActivating
//   Win32 side: WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
//              WS_EX_TOOLWINDOW applied via SetWindowLongPtr in showEvent.
//
// Phase 7-fix2b：交互范式收敛——覆盖层恒穿透（WS_EX_TRANSPARENT 常驻），移动
// 统一走 ControlBar 的 grip 握把拖动（ScreenTranslateController 调 move()）。
// 原 Phase 7-fix1 的锁定/解锁（setClickThrough 双态）与整窗拖动路径已移除。
//
// The window covers the captured region (LOGICAL global rect). Each text
// group paints a semi-transparent dark rounded panel (#202020 ~85%) at the
// group's original position with white translated text; while a translation
// is pending the source text is shown in translucent gray instead. Font size
// starts from the OCR line height and shrinks (>= 9pt) until the wrapped text
// fits; the panel may grow downwards up to 1.8x the group height.
//
// 坐标契约（BUG7 修复固化）：OverlayGroup::rect 是相对本窗口左上角的局部坐标，
// paintEvent 只按局部坐标绘制——move() 平移窗口时面板自然跟随，绝不因拖动
// 修改分组数据或重算面板几何。

#pragma once

#include <QRectF>
#include <QString>
#include <QVector>
#include <QWidget>

struct OverlayGroup {
    QRectF rect;            // LOGICAL px, relative to the overlay's top-left
    qreal lineHeight = 0.0; // LOGICAL px, from the OCR line height
    QString sourceText;     // shown translucent-gray while loading
    QString translatedText;
    bool loading = true;
};

class OverlayWindow : public QWidget
{
    Q_OBJECT
public:
    // `regionLogical` is the captured region in global logical coordinates.
    explicit OverlayWindow(const QRect &regionLogical, QWidget *parent = nullptr);

    void setGroups(const QVector<OverlayGroup> &groups);
    void setGroupTranslation(int index, const QString &text);
    int groupCount() const { return m_groups.size(); }

    // All finished translations joined by blank lines (for "复制译文").
    QString allTranslatedText() const;

    // Phase 3 placeholder: pin the region for periodic re-capture/refresh.
    // Intentionally a no-op in phase 2.
    void pinRegion(const QRect &regionLogical);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    QVector<OverlayGroup> m_groups;
};
