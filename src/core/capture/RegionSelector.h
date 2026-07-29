// RegionSelector - full-screen region selection mask.
//
// Design: ONE selector window PER SCREEN (instead of a single window spanning
// the virtual desktop). With per-monitor-v2 DPI a single spanning window gets
// exactly one scale factor from the OS, so on mixed-DPI setups the mask would
// be blurry / misplaced on every other monitor; per-screen windows are each
// rendered at their own native scale. Trade-off: one drag cannot cross a
// monitor boundary, which is acceptable for game translation.
//
// Each window shows a frozen screenshot of its screen darkened by ~35% black.
// Dragging reveals the selection at full brightness with a Phase 7-fix1
// "1px black + 1px white" double border, four corner handles and a
// "WxH px" size label (physical pixels).
//
// RegionSelectSession orchestrates the windows: hides the caller widget while
// selecting, restores it afterwards, and emits regionSelected(QRect global
// LOGICAL rect) or cancelled(). The session deletes itself when finished.

#pragma once

#include <QPixmap>
#include <QPointer>
#include <QRect>
#include <QVector>
#include <QWidget>

class QScreen;

class RegionSelector : public QWidget
{
    Q_OBJECT
public:
    explicit RegionSelector(QScreen *screen);

signals:
    void regionSelected(const QRect &globalLogicalRect);
    void selectionCancelled();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QRect selectionRect() const;   // local logical coords

    QScreen *m_screen = nullptr;
    QPixmap m_backdrop;            // frozen screen content (physical px, dpr set)
    QPoint m_origin;
    QPoint m_current;
    bool m_dragging = false;
};

class RegionSelectSession : public QObject
{
    Q_OBJECT
public:
    // `callerToHide` (may be null) is hidden during selection and restored
    // when the session ends.
    explicit RegionSelectSession(QWidget *callerToHide, QObject *parent = nullptr);

    void start();

signals:
    void regionSelected(const QRect &globalLogicalRect);
    void cancelled();

private:
    void openSelectors();
    void finish(bool selected, const QRect &globalLogicalRect);

    QPointer<QWidget> m_caller;
    QVector<RegionSelector *> m_selectors;
    bool m_callerWasVisible = false;
    bool m_finished = false;
};
