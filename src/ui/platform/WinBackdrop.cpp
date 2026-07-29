#include "ui/platform/WinBackdrop.h"

#ifdef _WIN32

#include "core/config/ConfigManager.h"

#  ifndef WINVER
#    define WINVER 0x0A00
#  endif
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0A00
#  endif
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <dwmapi.h>

#include <QDebug>
#include <QWindow>

namespace WinBackdrop {

// DWMWA_SYSTEMBACKDROP_TYPE (Win11 22H2+) 的常量，老 SDK 头可能没有。
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#  define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#  define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
// DWMSBT_MAINWINDOW=2 (Mica), DWMSBT_TRANSIENTWINDOW=3 (Acrylic),
// DWMSBT_TABBEDWINDOW=4, DWMSBT_AUTO=1, DWMSBT_NONE=0.

// SetWindowCompositionAttribute 是无文档 API，user32.dll 运行时加载。
// ACCENT_ENABLE_ACRYLICBLURBEHIND=3。结构布局参考 chromium / 内网流传版本。
struct ACCENTPOLICY {
    int nAccentState;
    int nFlags;
    int nColor;          // 0xAABBGGRR
    int nAnimationId;
};
struct WINCOMPATTRDATA {
    int nAttribute;      // 19 = WCA_ACCENT_POLICY
    PVOID pData;
    ULONG ulDataSize;
};

using PFN_SET_WINDOW_COMP_ATTR = BOOL(WINAPI *)(HWND, WINCOMPATTRDATA *);

PFN_SET_WINDOW_COMP_ATTR resolveSetWindowCompositionAttribute()
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32)
        return nullptr;
    return reinterpret_cast<PFN_SET_WINDOW_COMP_ATTR>(
        GetProcAddress(user32, "SetWindowCompositionAttribute"));
}

bool isDisabledByReduceTransparency()
{
    return ConfigManager::instance().reduceTransparency();
}

bool applyTopLevel(QWindow *win, bool dark)
{
    if (!win)
        return false;
    // 减少透明度开启时一律 no-op，让调用方走方案 3 假玻璃。
    if (isDisabledByReduceTransparency()) {
        qInfo().noquote() << QStringLiteral(
            "[backdrop] top-level: disabled by reduce_transparency");
        return false;
    }

    HWND hwnd = reinterpret_cast<HWND>(win->winId());
    if (!hwnd)
        return false;

    // 1) DWMWA_USE_IMMERSIVE_DARK_MODE 随主题切换。
    BOOL darkMode = dark ? TRUE : FALSE;
    HRESULT hr = DwmSetWindowAttribute(hwnd,
        DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
    if (FAILED(hr)) {
        qWarning().noquote() << QStringLiteral(
            "[backdrop] top-level: SetImmersiveDarkMode failed hr=0x%1")
                .arg(quint32(hr), 8, 16, QChar('0'));
        // 不直接 return：dark mode 失败不阻断 backdrop 本身。
    }

    // 2) DWMWA_SYSTEMBACKDROP_TYPE：Mica 或 Acrylic 由常量决定。
    INT backdropType = (kTopLevelBackdrop == TopLevelBackdrop::Acrylic) ? 3 : 2;
    hr = DwmSetWindowAttribute(hwnd,
        DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType));
    if (FAILED(hr)) {
        qWarning().noquote() << QStringLiteral(
            "[backdrop] top-level: SetSystemBackdrop failed hr=0x%1 "
            "(likely Win10 or older Win11 build, fallback to fake glass)")
                .arg(quint32(hr), 8, 16, QChar('0'));
        return false;
    }

    // 3) DwmExtendFrameIntoClientArea(-1 margins) 让客户区延伸到边框，
    //    否则 Mica/Acrylic 只覆盖标题栏区域，客户区仍是默认背景。
    MARGINS margins = { -1, -1, -1, -1 };
    hr = DwmExtendFrameIntoClientArea(hwnd, &margins);
    if (FAILED(hr)) {
        qWarning().noquote() << QStringLiteral(
            "[backdrop] top-level: ExtendFrameIntoClientArea failed hr=0x%1")
                .arg(quint32(hr), 8, 16, QChar('0'));
        // 仍返回 true：backdrop 已设置，只是客户区可能不延伸
    }

    const char *name = (kTopLevelBackdrop == TopLevelBackdrop::Acrylic)
                       ? "Acrylic" : "Mica";
    qInfo().noquote() << QStringLiteral(
        "[backdrop] top-level: %1 applied (dark=%2, hwnd=0x%3)")
            .arg(QString::fromLatin1(name))
            .arg(dark ? 1 : 0)
            .arg(quintptr(hwnd), 0, 16);
    return true;
}

bool applyAcrylicPopup(QWindow *win)
{
    if (!win)
        return false;
    if (isDisabledByReduceTransparency()) {
        qInfo().noquote() << QStringLiteral(
            "[backdrop] popup: disabled by reduce_transparency");
        return false;
    }

    HWND hwnd = reinterpret_cast<HWND>(win->winId());
    if (!hwnd)
        return false;

    auto pfn = resolveSetWindowCompositionAttribute();
    if (!pfn) {
        qInfo().noquote() << QStringLiteral(
            "[backdrop] popup: SetWindowCompositionAttribute unavailable, "
            "fallback to fake glass");
        return false;
    }

    ACCENTPOLICY policy;
    policy.nAccentState = 3;        // ACCENT_ENABLE_ACRYLICBLURBEHIND
    policy.nFlags = 0;
    // 颜色 0xAABBGGRR：浅色底 rgba(255,255,255,0.60) → 0x99FFFFFF
    // 由调用方 paintEvent 实际绘制玻璃面，这里仅给模糊层一个底色参考。
    // 用深色底避免浅色壁纸穿透刺眼；调用方 paintEvent 会覆盖真实玻璃色。
    policy.nColor = 0x99000000;
    policy.nAnimationId = 0;

    WINCOMPATTRDATA data;
    data.nAttribute = 19;           // WCA_ACCENT_POLICY
    data.pData = &policy;
    data.ulDataSize = sizeof(policy);

    BOOL ok = pfn(hwnd, &data);
    if (!ok) {
        qWarning().noquote() << QStringLiteral(
            "[backdrop] popup: SetWindowCompositionAttribute failed, "
            "fallback to fake glass");
        return false;
    }
    qInfo().noquote() << QStringLiteral(
        "[backdrop] popup: Acrylic applied (hwnd=0x%1)")
            .arg(quintptr(hwnd), 0, 16);
    return true;
}

int queryBackdrop(QWindow *win)
{
    if (!win)
        return 0;
    HWND hwnd = reinterpret_cast<HWND>(win->winId());
    if (!hwnd)
        return 0;
    INT type = 0;
    // Windows SDK 10.0.26100 头文件的 DwmGetWindowAttribute 是 4 参数版本
    // （无 pcbResult 出参）。MSDN 文档的 5 参数签名对应更新版本，这里按
    // 实际 SDK 头调用。失败返回 0（未设置）。
    HRESULT hr = DwmGetWindowAttribute(hwnd,
        DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type));
    if (FAILED(hr))
        return 0;
    return int(type);
}

} // namespace WinBackdrop

#endif // _WIN32
