// ThemeManager - light/dark/system theming (phase 4) + Liquid Glass tokens (phase 7).
//
// apply(mode) loads the QSS (glass_template.qss rendered with the live token
// values when reduce_transparency=false, otherwise the static {light,dark}.qss
// fallback) and refreshes the ThemePalette constants used by the painted
// (non-widget) surfaces: OverlayWindow panels, ControlBar background,
// RegionSelector mask and the tray icon accent. "system" resolves via
// QStyleHints::colorScheme and re-applies live on colorSchemeChanged.
//
// Phase 7 Liquid Glass: the palette now carries glass tokens (surface alpha,
// border, specular highlight, radii, shadow, text-on-glass). The QSS template
// at :/themes/glass_template.qss uses {{placeholder}} markers that this
// manager fills in at apply time. If the template is missing or contains
// residual placeholders after rendering, we fall back to the static QSS and
// emit qWarning; --selftest theme checks the rendered product (qss_ok stays a
// boolean, contract preserved).
//
// The singleton also listens to ConfigManager("theme") and
// ("ui.reduce_transparency") so the settings combo / accessibility toggle
// take effect immediately.

#pragma once

#include <QColor>
#include <QObject>
#include <QString>

// Shared color constants for painted surfaces; widget styling lives in the
// QSS files. Keep both in sync when tuning the theme.
struct ThemePalette {
    bool dark = false;
    // Phase 7-fix1：配色系统全面去橙，改黑白灰高质感。
    // accent 现为灰阶主色（浅色=#111111 黑底 / 深色=#F5F5F5 白底），
    // 配合 accentText 形成"反色按钮"（苹果式主操作黑白互换）。
    // 字段名保留 accent 不动以最小化 QSS 模板与 paintEvent 引用改动。
    QColor accent;        // 灰阶主色（原品牌橙已移除，现黑/白互换）
    QColor accentHover;
    QColor accentText;    // 主按钮文字色（与 accent 反色）
    QColor accentPressed; // pressed 态（取代原硬编码品牌色）
    QColor windowBg;
    QColor cardBg;
    QColor cardBorder;
    QColor text;
    QColor subText;
    QColor panelBg;       // overlay translated-text panel
    QColor panelText;
    QColor panelLoading;  // translucent source text while translating
    QColor barBg;         // floating control bar background

    // ---- Phase 7-fix1：灰阶次按钮/焦点/选中令牌（最小集，避免与 accent 双体系并存） ----
    QColor focusBorder;             // 1.5px focus 描边
    QColor secondaryButtonBorder;   // 次按钮描边
    QColor secondaryButtonText;      // 次按钮文字
    QColor secondaryButtonBg;        // Phase 7-fix2：次按钮独立底色（不依赖 glass_surface 透底）
    QColor selectionBg;              // 选中项灰底（半透明）
    QColor selectionLeftBar;         // 选中项左条 3px

    // ---- Phase 7: Liquid Glass tokens (任务 B 节) ----
    // 玻璃面：真模糊之上 浅 0.42/深 0.40（Acrylic 通透感），假玻璃场景 0.82/0.85（保证可读）
    QColor glassSurface;        // 真模糊层之上的玻璃面底色（带 alpha）
    QColor glassSurfaceFake;    // 假玻璃场景（OverlayWindow / 回退）的底色
    // 阅读焦点卡片（resultCard）独立 alpha 0.65/0.68，高于普通玻璃面保证译文可读
    QColor resultCardSurface;
    // 高光与描边
    QColor glassBorder;         // 1px 外描边
    QColor glassSpecular;       // 卡片顶部内侧 1px specular 高光
    // 圆角同心体系（窗口内容区 20 → 卡片 16 → 卡片内控件 10，按钮胶囊 10）
    int glassRadiusWindow = 20;
    int glassRadiusCard = 16;
    int glassRadiusControl = 10;
    // 阴影色（0 8px 32px）
    QColor glassShadow;
    // 文字对比度硬约束（≥4.5:1）
    QColor textOnGlass;
};

class ThemeManager : public QObject
{
    Q_OBJECT
public:
    static ThemeManager &instance();

    // mode: "light" / "dark" / "system". Anything else falls back to light.
    void apply(const QString &mode);
    // Re-applies the mode stored in ConfigManager (startup entry point).
    void applyFromConfig();

    QString mode() const { return m_mode; }
    bool isDark() const { return m_palette.dark; }
    const ThemePalette &palette() const { return m_palette; }

    // Phase 7: Liquid Glass 可达性开关（ui.reduce_transparency）。
    // 开启或系统不支持 backdrop 时，全部玻璃面退化为不透明卡片。
    bool reduceTransparency() const { return m_reduceTransparency; }
    void setReduceTransparency(bool on);

    // Raw QSS text from the resource (":/themes/<name>.qss"); empty when the
    // resource is missing. Exposed for --selftest theme.
    static QString loadQss(const QString &name);

    // Phase 7: 渲染 glass_template.qss 为最终 QSS（替换 {{token}} 占位符）。
    // 模板缺失或渲染失败时返回空字符串（调用方应回退到 loadQss）。
    // exposed公开仅供 --selftest theme 验证渲染产物。
    static QString renderGlassQss(const ThemePalette &pal, bool reduced);

    // Phase 7: 检查渲染产物是否仍含残留占位符（"{{" 或 "}}")。
    // 用于 --selftest theme 的 qss_ok 加固断言与运行时 qWarning。
    static bool hasResidualPlaceholders(const QString &rendered);

signals:
    void themeChanged();
    // Phase 7: reduce_transparency 切换时触发，所有真/假玻璃窗口监听
    // 统一退化或恢复。
    void reduceTransparencyChanged(bool reduced);

private:
    ThemeManager();
    void applyResolved(bool dark);

    QString m_mode = QStringLiteral("light");
    ThemePalette m_palette;
    bool m_reduceTransparency = false;
};
