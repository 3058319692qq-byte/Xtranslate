#include "ui/theme/ThemeManager.h"

#include "core/config/ConfigManager.h"

#include <QApplication>
#include <QFile>
#include <QStyleHints>

namespace {

// 把 QColor 转为 QSS 可用的 rgba(...) 字符串。alpha 未设时取 1.0。
QString rgba(const QColor &c)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(c.red()).arg(c.green()).arg(c.blue())
        .arg(c.alphaF() <= 0.0 ? 1.0 : c.alphaF());
}

ThemePalette lightPalette()
{
    ThemePalette p;
    p.dark = false;
    // Phase 7-fix1：去橙改灰阶主色（黑底白字反色按钮，苹果式）
    p.accent = QColor(0x11, 0x11, 0x11);         // 主按钮底 #111111
    p.accentHover = QColor(0x2A, 0x2A, 0x2A);    // hover #2A2A2A
    p.accentText = QColor(0xFF, 0xFF, 0xFF);     // 主按钮文字白
    p.accentPressed = QColor(0x00, 0x00, 0x00);  // pressed #000000
    // 次按钮/焦点/选中灰阶令牌（最小集）
    p.focusBorder = QColor(0x6B, 0x72, 0x80);           // 1.5px focus #6B7280
    // Phase 7-fix2：次按钮描边提亮到 focusBorder 同色，原 #D1D5DB 对比度 ~1.2:1 远低于 WCAG 3:1
    p.secondaryButtonBorder = QColor(0x6B, 0x72, 0x80); // 次按钮描边 #6B7280（提亮）
    p.secondaryButtonText = QColor(0x1F, 0x29, 0x37);    // 次按钮文字 #1F2937
    // Phase 7-fix2：次按钮独立底色，不依赖 glass_surface 透底（BUG3 根治）
    p.secondaryButtonBg = QColor(0, 0, 0, 15);            // rgba(0,0,0,0.06)
    p.selectionBg = QColor(0, 0, 0, 15);                 // rgba(0,0,0,0.06)
    p.selectionLeftBar = QColor(0x11, 0x11, 0x11);       // 左条 #111111

    p.windowBg = QColor(0xFA, 0xF7, 0xF2);
    p.cardBg = QColor(0xFF, 0xFF, 0xFF);
    p.cardBorder = QColor(0xE4, 0xDC, 0xD0);
    p.text = QColor(0x33, 0x29, 0x1A);
    p.subText = QColor(0x8A, 0x80, 0x72);
    p.panelBg = QColor(0x20, 0x20, 0x20, 217);      // #202020 @ ~85%
    p.panelText = QColor(Qt::white);
    p.panelLoading = QColor(210, 210, 210, 170);
    p.barBg = QColor(0x20, 0x20, 0x20, 235);

    // Phase 7-fix1：玻璃面 alpha 下调让 Acrylic 通透感透出
    // 真模糊之上：rgba(255,255,255,0.42)；假玻璃场景：0.82 保证可读（不动）
    p.glassSurface = QColor(255, 255, 255, 107);    // 0.42（原 0.60）
    p.glassSurfaceFake = QColor(255, 255, 255, 209); // 0.82（不动，假玻璃仍需高 alpha）
    // resultCard 阅读焦点：0.65，高于普通玻璃面保证译文区可读
    p.resultCardSurface = QColor(255, 255, 255, 166); // 0.65
    p.glassBorder = QColor(255, 255, 255, 89);      // 0.35
    p.glassSpecular = QColor(255, 255, 255, 140);   // 0.55
    p.glassRadiusWindow = 20;
    p.glassRadiusCard = 16;
    p.glassRadiusControl = 10;
    p.glassShadow = QColor(0, 0, 0, 41);            // 0.16
    // 浅色玻璃面用 #1F2937 保证 ≥4.5:1 对比度
    p.textOnGlass = QColor(0x1F, 0x29, 0x37);
    return p;
}

ThemePalette darkPalette()
{
    ThemePalette p;
    p.dark = true;
    // Phase 7-fix1：去橙改灰阶主色（白底黑字反色按钮，苹果式）
    p.accent = QColor(0xF5, 0xF5, 0xF5);         // 主按钮底 #F5F5F5
    p.accentHover = QColor(0xFF, 0xFF, 0xFF);    // hover #FFFFFF
    p.accentText = QColor(0x11, 0x11, 0x11);     // 主按钮文字 #111111
    p.accentPressed = QColor(0xE0, 0xE0, 0xE0);  // pressed #E0E0E0
    // 次按钮/焦点/选中灰阶令牌（最小集）
    p.focusBorder = QColor(0x9C, 0xA3, 0xAF);           // 1.5px focus #9CA3AF
    // Phase 7-fix2：次按钮描边提亮到 focusBorder 同色，原 #3A3D45 对比度 ~1.3:1 远低于 WCAG 3:1
    p.secondaryButtonBorder = QColor(0x9C, 0xA3, 0xAF); // 次按钮描边 #9CA3AF（提亮）
    p.secondaryButtonText = QColor(0xE7, 0xE9, 0xEE);    // 次按钮文字 #E7E9EE
    // Phase 7-fix2：次按钮独立底色，不依赖 glass_surface 透底（BUG3 根治）
    p.secondaryButtonBg = QColor(255, 255, 255, 36);      // rgba(255,255,255,0.14)
    p.selectionBg = QColor(255, 255, 255, 20);            // rgba(255,255,255,0.08)
    p.selectionLeftBar = QColor(0xF5, 0xF5, 0xF5);       // 左条 #F5F5F5

    p.windowBg = QColor(0x1E, 0x1B, 0x18);
    p.cardBg = QColor(0x2A, 0x26, 0x21);
    p.cardBorder = QColor(0x45, 0x3E, 0x35);
    p.text = QColor(0xEE, 0xE7, 0xDC);
    p.subText = QColor(0xA8, 0x9F, 0x91);
    p.panelBg = QColor(0x12, 0x12, 0x12, 225);      // deeper panel on dark
    p.panelText = QColor(0xF5, 0xF0, 0xE8);
    p.panelLoading = QColor(190, 190, 190, 170);
    p.barBg = QColor(0x12, 0x12, 0x12, 240);

    // Phase 7-fix1：玻璃面 alpha 下调让 Acrylic 通透感透出
    // 真模糊之上：rgba(30,32,38,0.40)；假玻璃场景：0.85 保证可读（不动）
    p.glassSurface = QColor(30, 32, 38, 102);       // 0.40（原 0.58）
    p.glassSurfaceFake = QColor(30, 32, 38, 217);   // 0.85（不动）
    // resultCard 阅读焦点：0.68，高于普通玻璃面保证译文区可读
    p.resultCardSurface = QColor(30, 32, 38, 173);   // 0.68
    p.glassBorder = QColor(255, 255, 255, 31);      // 0.12
    p.glassSpecular = QColor(255, 255, 255, 51);    // 0.20
    p.glassRadiusWindow = 20;
    p.glassRadiusCard = 16;
    p.glassRadiusControl = 10;
    p.glassShadow = QColor(0, 0, 0, 115);           // 0.45
    // 深色玻璃面用 #ECEEF2 保证 ≥4.5:1 对比度
    p.textOnGlass = QColor(0xEC, 0xEE, 0xF2);
    return p;
}

} // namespace

ThemeManager &ThemeManager::instance()
{
    static ThemeManager mgr;
    return mgr;
}

ThemeManager::ThemeManager()
    : m_palette(lightPalette())
{
    // "system" mode follows the OS live.
    QStyleHints *hints = QApplication::styleHints();
    connect(hints, &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme scheme) {
        if (m_mode == QLatin1String("system"))
            applyResolved(scheme == Qt::ColorScheme::Dark);
    });

    // Settings combo writes config("theme"); react immediately.
    // Phase 7: 同样监听 ui.reduce_transparency，触发即时退化/恢复。
    connect(&ConfigManager::instance(), &ConfigManager::configChanged, this,
            [this](const QString &path) {
        if (path == QLatin1String("theme"))
            applyFromConfig();
        else if (path == QLatin1String("ui.reduce_transparency"))
            setReduceTransparency(
                ConfigManager::instance().reduceTransparency());
    });
}

QString ThemeManager::loadQss(const QString &name)
{
    QFile file(QStringLiteral(":/themes/%1.qss").arg(name));
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll());
}

void ThemeManager::applyFromConfig()
{
    apply(ConfigManager::instance().theme());
    setReduceTransparency(ConfigManager::instance().reduceTransparency());
}

void ThemeManager::apply(const QString &mode)
{
    m_mode = mode.isEmpty() ? QStringLiteral("light") : mode;
    bool dark = false;
    if (m_mode == QLatin1String("dark")) {
        dark = true;
    } else if (m_mode == QLatin1String("system")) {
        dark = QApplication::styleHints()->colorScheme()
               == Qt::ColorScheme::Dark;
    }
    applyResolved(dark);
}

void ThemeManager::setReduceTransparency(bool on)
{
    if (m_reduceTransparency == on)
        return;
    m_reduceTransparency = on;
    // 重新应用当前主题以切换 QSS 路径（玻璃模板 vs 静态回退）。
    applyResolved(m_palette.dark);
    emit reduceTransparencyChanged(on);
}

bool ThemeManager::hasResidualPlaceholders(const QString &rendered)
{
    // 模板使用 {{name}} 风格；渲染后不应出现 {{ 或 }}。
    return rendered.contains(QStringLiteral("{{"))
           || rendered.contains(QStringLiteral("}}"));
}

QString ThemeManager::renderGlassQss(const ThemePalette &pal, bool reduced)
{
    QFile tpl(QStringLiteral(":/themes/glass_template.qss"));
    if (!tpl.open(QIODevice::ReadOnly))
        return QString();   // 调用方回退到 loadQss

    QString out = QString::fromUtf8(tpl.readAll());
    tpl.close();

    // 减少透明度模式：玻璃面退化为不透明卡片主题。
    // 沿用现有 light/dark 面板色，alpha 拉满。
    const QColor surface = reduced ? pal.cardBg : pal.glassSurface;
    const QColor surfaceFake = reduced ? pal.cardBg : pal.glassSurfaceFake;
    const QColor border = reduced ? pal.cardBorder : pal.glassBorder;
    const QColor specular = reduced ? QColor(0, 0, 0, 0) : pal.glassSpecular;
    const QColor shadow = reduced ? QColor(0, 0, 0, 0) : pal.glassShadow;
    const QColor text = pal.textOnGlass;

    // 替换占位符（注意顺序：先长后短避免前缀冲突）。
    out.replace(QStringLiteral("{{glass_surface_fake}}"), rgba(surfaceFake));
    out.replace(QStringLiteral("{{glass_surface}}"), rgba(surface));
    // Phase 7-fix1：resultCard 独立 alpha（不受 reduce_transparency 切换影响，
    // reduce 时退化为 cardBg，由 surface 变量替代——这里仅在非 reduce 时使用真值）
    out.replace(QStringLiteral("{{result_card_surface}}"),
                rgba(reduced ? pal.cardBg : pal.resultCardSurface));
    out.replace(QStringLiteral("{{glass_border}}"), rgba(border));
    out.replace(QStringLiteral("{{glass_specular}}"), rgba(specular));
    out.replace(QStringLiteral("{{glass_shadow}}"), rgba(shadow));
    out.replace(QStringLiteral("{{text_on_glass}}"), rgba(text));
    out.replace(QStringLiteral("{{accent}}"),
                QStringLiteral("#%1").arg(pal.accent.rgba(), 6, 16, QChar('0')));
    out.replace(QStringLiteral("{{accent_hover}}"),
                QStringLiteral("#%1").arg(pal.accentHover.rgba(), 6, 16, QChar('0')));
    // Phase 7-fix1：新增灰阶令牌占位符（与 ThemePalette 字段一一对应）
    out.replace(QStringLiteral("{{accent_text}}"),
                QStringLiteral("#%1").arg(pal.accentText.rgba(), 6, 16, QChar('0')));
    out.replace(QStringLiteral("{{accent_pressed}}"),
                QStringLiteral("#%1").arg(pal.accentPressed.rgba(), 6, 16, QChar('0')));
    out.replace(QStringLiteral("{{focus_border}}"),
                QStringLiteral("#%1").arg(pal.focusBorder.rgba(), 6, 16, QChar('0')));
    out.replace(QStringLiteral("{{secondary_button_border}}"),
                QStringLiteral("#%1").arg(pal.secondaryButtonBorder.rgba(), 6, 16, QChar('0')));
    out.replace(QStringLiteral("{{secondary_button_text}}"),
                QStringLiteral("#%1").arg(pal.secondaryButtonText.rgba(), 6, 16, QChar('0')));
    // Phase 7-fix2：次按钮独立底色。reduce_transparency 时退化为 cardBg 不透明。
    out.replace(QStringLiteral("{{secondary_button_bg}}"),
                rgba(reduced ? pal.cardBg : pal.secondaryButtonBg));
    // 选中项半透明灰底必须用 rgba 才能带 alpha
    out.replace(QStringLiteral("{{selection_bg}}"), rgba(pal.selectionBg));
    out.replace(QStringLiteral("{{selection_left_bar}}"),
                QStringLiteral("#%1").arg(pal.selectionLeftBar.rgba(), 6, 16, QChar('0')));
    out.replace(QStringLiteral("{{radius_window}}"),
                QString::number(pal.glassRadiusWindow));
    out.replace(QStringLiteral("{{radius_card}}"),
                QString::number(pal.glassRadiusCard));
    out.replace(QStringLiteral("{{radius_control}}"),
                QString::number(pal.glassRadiusControl));
    // 通用文本色与窗口底色（玻璃化下窗口背景透明，仅减少透明度时用）
    out.replace(QStringLiteral("{{window_bg}}"),
                QStringLiteral("#%1").arg(pal.windowBg.rgba(), 6, 16, QChar('0')));
    out.replace(QStringLiteral("{{text}}"),
                QStringLiteral("#%1").arg(pal.text.rgba(), 6, 16, QChar('0')));
    out.replace(QStringLiteral("{{sub_text}}"),
                QStringLiteral("#%1").arg(pal.subText.rgba(), 6, 16, QChar('0')));
    out.replace(QStringLiteral("{{card_bg}}"),
                QStringLiteral("#%1").arg(pal.cardBg.rgba(), 6, 16, QChar('0')));
    out.replace(QStringLiteral("{{card_border}}"),
                QStringLiteral("#%1").arg(pal.cardBorder.rgba(), 6, 16, QChar('0')));

    if (hasResidualPlaceholders(out)) {
        qWarning().noquote() << QStringLiteral(
            "[theme] glass QSS render has residual placeholders, "
            "falling back to static qss");
        return QString();
    }
    return out;
}

void ThemeManager::applyResolved(bool dark)
{
    m_palette = dark ? darkPalette() : lightPalette();

    // Phase 7: 玻璃模板优先（仅当未开启减少透明度时）。
    // 模板缺失/渲染失败/残留占位符 → 回退静态 {light,dark}.qss，
    // UI 永远有样式可用（任务书约束 3b）。
    QString qss;
    if (!m_reduceTransparency) {
        qss = renderGlassQss(m_palette, false);
    }
    if (qss.isEmpty()) {
        qss = loadQss(dark ? QStringLiteral("dark")
                           : QStringLiteral("light"));
    }
    qApp->setStyleSheet(qss);
    emit themeChanged();
}
