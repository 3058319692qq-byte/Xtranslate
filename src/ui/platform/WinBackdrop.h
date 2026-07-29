// WinBackdrop - Windows 11 DWM backdrop + Acrylic 应用统一入口（Phase 7）。
//
// 任务 A 节分层实现策略的封装：
//   方案 1（真模糊·顶层窗口）：Mica (DWMSBT_MAINWINDOW) 或
//                                Acrylic (DWMSBT_TRANSIENTWINDOW)，二选一
//                                由 kTopLevelBackdrop 常量决定（一行切换）。
//                                + DWMWA_USE_IMMERSIVE_DARK_MODE 随主题切换
//                                + DwmExtendFrameIntoClientArea(-1 margins)
//   方案 2（真模糊·无边框小窗）：SetWindowCompositionAttribute
//                                ACCENT_ENABLE_ACRYLICBLURBEHIND（无文档 API，
//                                运行时 GetProcAddress，失败回退方案 3）
//   方案 3（假玻璃·必须自绘）：调用方走 ThemeManager::palette().glassSurfaceFake
//
// 减少透明度（ui.reduce_transparency）开启时：applyMica/applyAcrylic 一律
// no-op + 返回 false，调用方按方案 3 处理。
//
// 所有调用方应在 showEvent 内调用，并在 reports/phase7_dev.md 记录实际
// 生效方案与回退情况。queryBackdrop() 用 DwmGetWindowAttribute 反读
// 验证，写入日志作为 backdrop 生效证明（任务 D 节）。

#pragma once

#ifdef _WIN32

#include <QWindow>

namespace WinBackdrop {

// 一行配置：顶层窗口（MainWindow/SettingsWindow）的 backdrop 类型。
// Mica = DWMSBT_MAINWINDOW（壁纸淡染，性能优，"磨砂桌面"观感）
// Acrylic = DWMSBT_TRANSIENTWINDOW（前景模糊，更"玻璃"，性能开销大）
// 任务书要求 A/B 对照后由用户拍板，切换成本必须低 → 改这一行常量即可。
enum class TopLevelBackdrop {
    Mica,
    Acrylic,
};
// Phase 7-fix1：用户判定 Mica 不是苹果玻璃，切 Acrylic（DWMSBT_TRANSIENTWINDOW）
// 让前景模糊真正透出来；OcrResultDialog 走 applyTopLevel 自动同步生效。
constexpr TopLevelBackdrop kTopLevelBackdrop = TopLevelBackdrop::Acrylic;

// 方案 1：顶层窗口 backdrop（Mica 或 Acrylic 由 kTopLevelBackdrop 决定）。
// dark=true 时同步设置 DWMWA_USE_IMMERSIVE_DARK_MODE。
// 返回 true=生效，false=系统不支持/减少透明度开启/调用失败（调用方走方案 3）。
// 会同时调用 DwmExtendFrameIntoClientArea(-1 margins) 让客户区延伸到边框。
bool applyTopLevel(QWindow *win, bool dark);

// 方案 2：无边框小窗 Acrylic（PopupCard/OcrResultDialog/ControlBar）。
// 运行时 GetProcAddress SetWindowCompositionAttribute，失败返回 false。
// 返回 true=生效，false=不支持/减少透明度开启/调用失败（调用方走方案 3）。
bool applyAcrylicPopup(QWindow *win);

// 反读 DWMWA_SYSTEMBACKDROP_TYPE 验证 backdrop 实际类型。
// 返回 0=未设置/查询失败，1=Auto，2=Mica，3=Acrylic，4=Tabbed。
int queryBackdrop(QWindow *win);

// 是否因减少透明度而禁用 backdrop（供调用方决策走方案 3）。
// 读 ConfigManager::reduceTransparency() 即时反映。
bool isDisabledByReduceTransparency();

} // namespace WinBackdrop

#endif // _WIN32
