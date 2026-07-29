// ControlBar - small clickable companion window for the overlay.
//
// Separate top-level tool window (NOT click-through, unlike OverlayWindow)
// docked at the top-right corner of the captured region. Never steals focus
// from the game: Qt::WindowDoesNotAcceptFocus + WS_EX_NOACTIVATE.
// Layout: grip 握把 | 重译 | 复制译文 | 隐藏/显示 | 关闭.
//
// Phase 7-fix2b：锁定/解锁按钮已移除（覆盖层恒穿透），grip 握把是唯一拖动
// 入口，始终可用。拖动协议：press 发 dragStarted()，move 发 dragRequested(
// 自拖动起点的累计 delta)，release 发 dragFinished()——控制器据 dragStarted
// 时记录的 overlay 起始位置做绝对定位（起点+delta），杜绝累计 delta 被增量
// 叠加导致的"覆盖层飞出屏幕"回归（BUG7）。

#pragma once

#include <QWidget>

class QPushButton;
class QLabel;

class ControlBar : public QWidget
{
    Q_OBJECT
public:
    explicit ControlBar(QWidget *parent = nullptr);

    // Dock at the top-right corner of `regionLogical` (global logical rect),
    // clamped to the screen so the bar stays reachable.
    void dockTo(const QRect &regionLogical);

    // Flip the toggle button label between 隐藏译文 / 显示译文.
    void setOverlayVisible(bool visible);

signals:
    void retranslateRequested();
    void copyRequested();
    void toggleOverlayRequested();
    void closeRequested();
    // Phase 7-fix2b：grip 拖动协议（见文件头）。dragRequested 的 delta 是
    // 自本次拖动起点（dragStarted 时刻）的累计位移，接收方必须用
    // "拖动起点位置 + delta" 做绝对定位，不可在当前位置上增量叠加。
    void dragStarted();
    void dragRequested(const QPoint &delta);
    void dragFinished();

protected:
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    // grip 握把的鼠标事件经 eventFilter 拦截实现拖动；不干扰按钮自身事件。
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Phase 7: 应用 Acrylic popup backdrop；失败回退 qss 假玻璃（不变契约）。
    void applyBackdrop();

    QPushButton *m_toggleButton = nullptr;
    // grip 握把（唯一拖动入口）。objectName=gripHandle，accessibleName="拖动"
    // 给 UIA（红线：可达性名字保留）。
    QLabel *m_grip = nullptr;
    // grip 拖动状态。
    bool m_gripDragging = false;
    QPoint m_gripStartGlobal;
    QPoint m_gripStartPos;
    // Phase 7: 标记当前是否处于真玻璃模式（Acrylic 生效中）。
    // paintEvent 据此切换底色：真玻璃用 glassSurface（半透明）让模糊透过；
    // 假玻璃/减少透明度时用 barBg（不透明）保持原观感。
    bool m_glassActive = false;
};
