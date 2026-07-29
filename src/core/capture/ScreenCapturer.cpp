#include "core/capture/ScreenCapturer.h"

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

#include <QGuiApplication>
#include <QScreen>

#include <cmath>
#include <cstring>
#include <vector>

namespace {

#ifdef _WIN32

struct NativeMonitor {
    QString deviceName;   // "\\.\DISPLAY1" - matches QScreen::name() on Windows
    QRect physicalRect;   // native pixels
};

BOOL CALLBACK enumMonitorProc(HMONITOR monitor, HDC, LPRECT, LPARAM userData)
{
    auto *list = reinterpret_cast<std::vector<NativeMonitor> *>(userData);
    MONITORINFOEXW info;
    std::memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
        NativeMonitor m;
        m.deviceName = QString::fromWCharArray(info.szDevice);
        m.physicalRect = QRect(info.rcMonitor.left, info.rcMonitor.top,
                               info.rcMonitor.right - info.rcMonitor.left,
                               info.rcMonitor.bottom - info.rcMonitor.top);
        list->push_back(m);
    }
    return TRUE;
}

std::vector<NativeMonitor> enumerateMonitors()
{
    std::vector<NativeMonitor> list;
    EnumDisplayMonitors(nullptr, nullptr, enumMonitorProc,
                        reinterpret_cast<LPARAM>(&list));
    return list;
}

// Native monitor rect for a QScreen: match by GDI device name first, fall
// back to the screen's index in QGuiApplication::screens().
QRect nativeRectForScreen(const QScreen *screen)
{
    const std::vector<NativeMonitor> monitors = enumerateMonitors();
    for (const NativeMonitor &m : monitors) {
        if (m.deviceName == screen->name())
            return m.physicalRect;
    }
    const QList<QScreen *> screens = QGuiApplication::screens();
    const int idx = static_cast<int>(screens.indexOf(screen));
    if (idx >= 0 && idx < static_cast<int>(monitors.size()))
        return monitors[static_cast<size_t>(idx)].physicalRect;
    // Last resort: assume the Qt logical origin is also the native origin.
    const qreal dpr = screen->devicePixelRatio();
    const QRect lg = screen->geometry();
    return QRect(lg.topLeft(),
                 QSize(qRound(lg.width() * dpr), qRound(lg.height() * dpr)));
}

#endif // _WIN32

// Screen containing a logical global point (nearest screen as fallback).
QScreen *screenForLogicalPoint(const QPointF &logicalGlobal)
{
    QScreen *screen = QGuiApplication::screenAt(logicalGlobal.toPoint());
    if (screen)
        return screen;
    QScreen *best = QGuiApplication::primaryScreen();
    qreal bestDist = -1.0;
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *s : screens) {
        const QRectF g = s->geometry();
        const qreal dx = qMax(qMax(g.left() - logicalGlobal.x(),
                                   logicalGlobal.x() - g.right()), 0.0);
        const qreal dy = qMax(qMax(g.top() - logicalGlobal.y(),
                                   logicalGlobal.y() - g.bottom()), 0.0);
        const qreal dist = dx * dx + dy * dy;
        if (bestDist < 0.0 || dist < bestDist) {
            bestDist = dist;
            best = s;
        }
    }
    return best;
}

#ifdef _WIN32

// Screen whose native monitor rect contains a physical global point.
QScreen *screenForPhysicalPoint(const QPoint &physicalGlobal)
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *s : screens) {
        if (nativeRectForScreen(s).contains(physicalGlobal))
            return s;
    }
    return QGuiApplication::primaryScreen();
}

#endif // _WIN32

} // namespace

QRect ScreenCapturer::virtualScreenPhysical()
{
#ifdef _WIN32
    return QRect(GetSystemMetrics(SM_XVIRTUALSCREEN),
                 GetSystemMetrics(SM_YVIRTUALSCREEN),
                 GetSystemMetrics(SM_CXVIRTUALSCREEN),
                 GetSystemMetrics(SM_CYVIRTUALSCREEN));
#else
    return QGuiApplication::primaryScreen()->virtualGeometry();
#endif
}

QImage ScreenCapturer::grabVirtualRect(const QRect &physicalRect)
{
#ifdef _WIN32
    const QRect rect = physicalRect.intersected(virtualScreenPhysical());
    if (rect.isEmpty())
        return QImage();

    HDC screenDc = GetDC(nullptr);
    if (!screenDc)
        return QImage();

    QImage result;
    HDC memDc = CreateCompatibleDC(screenDc);
    if (memDc) {
        BITMAPINFO bmi;
        std::memset(&bmi, 0, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = rect.width();
        bmi.bmiHeader.biHeight = -rect.height(); // negative -> top-down rows
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void *bits = nullptr;
        HBITMAP dib = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS,
                                       &bits, nullptr, 0);
        if (dib && bits) {
            HGDIOBJ old = SelectObject(memDc, dib);
            // CAPTUREBLT includes layered windows (tooltips, some overlays).
            if (BitBlt(memDc, 0, 0, rect.width(), rect.height(),
                       screenDc, rect.x(), rect.y(),
                       SRCCOPY | CAPTUREBLT)) {
                GdiFlush();
                // DIB rows are 4-byte aligned already at 32bpp; deep-copy so
                // the QImage outlives the DIB section.
                result = QImage(static_cast<const uchar *>(bits),
                                rect.width(), rect.height(),
                                rect.width() * 4,
                                QImage::Format_RGB32).copy();
            }
            SelectObject(memDc, old);
        }
        if (dib)
            DeleteObject(dib);
        DeleteDC(memDc);
    }
    ReleaseDC(nullptr, screenDc);
    return result;
#else
    Q_UNUSED(physicalRect);
    return QImage();
#endif
}

QImage ScreenCapturer::grabLogicalRect(const QRect &logicalRect)
{
    return grabVirtualRect(logicalToPhysical(logicalRect));
}

QPoint ScreenCapturer::logicalToPhysical(const QPointF &logicalGlobal)
{
    const QScreen *screen = screenForLogicalPoint(logicalGlobal);
    if (!screen)
        return logicalGlobal.toPoint();
#ifdef _WIN32
    const QRect native = nativeRectForScreen(screen);
#else
    const QRect native = screen->geometry();
#endif
    const qreal dpr = screen->devicePixelRatio();
    const QPointF offset = logicalGlobal - QPointF(screen->geometry().topLeft());
    return QPoint(native.x() + qRound(offset.x() * dpr),
                  native.y() + qRound(offset.y() * dpr));
}

QPointF ScreenCapturer::physicalToLogical(const QPoint &physicalGlobal)
{
#ifdef _WIN32
    const QScreen *screen = screenForPhysicalPoint(physicalGlobal);
    if (!screen)
        return QPointF(physicalGlobal);
    const QRect native = nativeRectForScreen(screen);
    const qreal dpr = screen->devicePixelRatio();
    return QPointF(screen->geometry().topLeft())
           + QPointF((physicalGlobal.x() - native.x()) / dpr,
                     (physicalGlobal.y() - native.y()) / dpr);
#else
    return QPointF(physicalGlobal);
#endif
}

QRect ScreenCapturer::logicalToPhysical(const QRect &logicalGlobal)
{
    // Map through the screen owning the rect center so top-left and size use
    // one consistent dpr (selection rects never straddle monitors).
    const QScreen *screen = screenForLogicalPoint(QRectF(logicalGlobal).center());
    if (!screen)
        return logicalGlobal;
#ifdef _WIN32
    const QRect native = nativeRectForScreen(screen);
#else
    const QRect native = screen->geometry();
#endif
    const qreal dpr = screen->devicePixelRatio();
    const QPointF offset = QPointF(logicalGlobal.topLeft())
                           - QPointF(screen->geometry().topLeft());
    return QRect(native.x() + qRound(offset.x() * dpr),
                 native.y() + qRound(offset.y() * dpr),
                 qRound(logicalGlobal.width() * dpr),
                 qRound(logicalGlobal.height() * dpr));
}

QRect ScreenCapturer::physicalToLogical(const QRect &physicalGlobal)
{
#ifdef _WIN32
    const QScreen *screen = screenForPhysicalPoint(physicalGlobal.center());
    if (!screen)
        return physicalGlobal;
    const QRect native = nativeRectForScreen(screen);
    const qreal dpr = screen->devicePixelRatio();
    const QPointF logicalTopLeft =
        QPointF(screen->geometry().topLeft())
        + QPointF((physicalGlobal.x() - native.x()) / dpr,
                  (physicalGlobal.y() - native.y()) / dpr);
    return QRect(logicalTopLeft.toPoint(),
                 QSize(qRound(physicalGlobal.width() / dpr),
                       qRound(physicalGlobal.height() / dpr)));
#else
    return physicalGlobal;
#endif
}
