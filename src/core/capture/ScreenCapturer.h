// ScreenCapturer - Win32 virtual-desktop capture + physical/logical mapping.
//
// Coordinate model (process is PER_MONITOR_AWARE_V2):
//   * PHYSICAL coordinates: native Win32 pixels. The virtual screen spans
//     SM_XVIRTUALSCREEN/SM_YVIRTUALSCREEN .. + SM_CX/CYVIRTUALSCREEN and may
//     start at negative offsets. BitBlt / GetDC(NULL) operate here.
//   * LOGICAL coordinates: Qt's device-independent pixels. Each QScreen has
//     its own devicePixelRatio(), so the mapping is per-screen affine:
//       physical = nativeTopLeft(screen) + (logical - logicalTopLeft(screen)) * dpr
//     QScreen is matched to its native monitor rect by GDI device name
//     (QScreen::name() == MONITORINFOEXW.szDevice), with an index fallback.
//
// Capture uses GetDC(NULL) + CreateCompatibleDC + BitBlt(SRCCOPY|CAPTUREBLT)
// into a 32bpp top-down DIB section instead of QScreen::grabWindow, which is
// unreliable across mixed-DPI multi-monitor setups.

#pragma once

#include <QImage>
#include <QPointF>
#include <QRect>
#include <QRectF>

class ScreenCapturer
{
public:
    // Grabs `physicalRect` (native virtual-screen pixels). The returned image
    // is Format_RGB32 at 1:1 physical resolution with devicePixelRatio == 1
    // (raw pixels are what the OCR pipeline wants). Null image on failure or
    // when the rect does not intersect the virtual screen.
    static QImage grabVirtualRect(const QRect &physicalRect);

    // Convenience: converts a logical (Qt global) rect and grabs it.
    static QImage grabLogicalRect(const QRect &logicalRect);

    // Native bounds of the whole virtual desktop (physical pixels).
    static QRect virtualScreenPhysical();

    // Per-screen affine mapping helpers. Points/rects are GLOBAL coordinates.
    // Rect variants map the top-left through the screen containing the rect
    // center and scale the size by that screen's dpr (a rect fully inside one
    // monitor - the only case the selection UI produces - maps exactly).
    static QPoint logicalToPhysical(const QPointF &logicalGlobal);
    static QPointF physicalToLogical(const QPoint &physicalGlobal);
    static QRect logicalToPhysical(const QRect &logicalGlobal);
    static QRect physicalToLogical(const QRect &physicalGlobal);
};
