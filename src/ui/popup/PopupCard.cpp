#include "ui/popup/PopupCard.h"

#include "core/config/ConfigManager.h"
#include "core/storage/HistoryStore.h"
#include "core/translate/TranslationManager.h"
#include "core/tts/TtsManager.h"
#include "ui/theme/ThemeManager.h"

#ifdef _WIN32
#  include "ui/platform/WinBackdrop.h"
#endif

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QFutureWatcher>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QScreen>
#include <QShowEvent>
#include <QSize>
#include <QVBoxLayout>

namespace {

constexpr int kCardWidth = 360;
constexpr int kShadowMargin = 14;   // translucent border reserved for shadow

// Truncate to at most 3 wrapped lines (approximation via character budget
// derived from the label width), appending an ellipsis.
QString clampToThreeLines(const QString &text, const QFont &font, int widthPx)
{
    const QFontMetrics fm(font);
    QString flat = text;
    flat.replace(QLatin1Char('\n'), QLatin1Char(' '));
    const int budget = widthPx * 3;
    if (fm.horizontalAdvance(flat) <= budget)
        return flat;
    // Binary-search the cut point.
    int lo = 0, hi = flat.size();
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (fm.horizontalAdvance(flat.left(mid)) <= budget)
            lo = mid;
        else
            hi = mid - 1;
    }
    return flat.left(qMax(0, lo - 1)) + QStringLiteral("…");
}

// Phase 7: 图标按钮工厂——objectName=iconButton 走 qss 模板的玻璃胶囊样式。
// 保留 text 作为 accessibleName 给 UIA / 屏幕阅读器（红线：可达性名字不删）。
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
    btn->setFixedSize(32, 28);
    return btn;
}

} // namespace

PopupCard::PopupCard(const QString &sourceText, QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint
                          | Qt::WindowStaysOnTopHint)
    , m_sourceText(sourceText)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setFixedWidth(kCardWidth + kShadowMargin * 2);

    // Source language guess drives the target: CJK -> en, otherwise zh-CN.
    m_targetLang = TtsManager::guessLang(sourceText) == QLatin1String("zh-CN")
                       ? QStringLiteral("en")
                       : QStringLiteral("zh-CN");

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(kShadowMargin, kShadowMargin,
                                   kShadowMargin, kShadowMargin);

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("popupCard"));
    auto *shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(22);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 90));
    card->setGraphicsEffect(shadow);
    rootLayout->addWidget(card);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 10);
    layout->setSpacing(8);

    m_sourceLabel = new QLabel(card);
    m_sourceLabel->setObjectName(QStringLiteral("sourceLabel"));
    m_sourceLabel->setWordWrap(true);
    m_sourceLabel->setText(clampToThreeLines(m_sourceText,
                                             m_sourceLabel->font(),
                                             kCardWidth - 28));
    m_sourceLabel->setToolTip(m_sourceText);
    layout->addWidget(m_sourceLabel);

    auto *line = new QFrame(card);
    line->setObjectName(QStringLiteral("cardLine"));
    line->setFrameShape(QFrame::HLine);
    layout->addWidget(line);

    m_resultLabel = new QLabel(tr("翻译中…"), card);
    m_resultLabel->setObjectName(QStringLiteral("resultLabel"));
    m_resultLabel->setWordWrap(true);
    m_resultLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_resultLabel);

    // Phase 7: 图标化按钮（copy/speak/close），accessibleName 保留原中文
    // 文案给 UIA 与屏幕阅读器，红线：既有 sourceLabel/resultLabel/popupCard
    // objectName 不动；新增按钮 objectName=iconButton（qss 模板已有规则）。
    auto *buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(6);
    m_copyButton = makeIconButton(QStringLiteral(":/icons/copy.svg"),
                                  tr("复制译文"), card);
    m_speakButton = makeIconButton(QStringLiteral(":/icons/speak.svg"),
                                   tr("朗读"), card);
    auto *closeButton = makeIconButton(QStringLiteral(":/icons/close.svg"),
                                       tr("关闭"), card);
    m_copyButton->setEnabled(false);
    m_speakButton->setEnabled(false);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_copyButton);
    buttonRow->addWidget(m_speakButton);
    buttonRow->addWidget(closeButton);
    layout->addLayout(buttonRow);

    connect(m_copyButton, &QPushButton::clicked, this, [this]() {
        if (!m_translatedText.isEmpty())
            QApplication::clipboard()->setText(m_translatedText);
    });
    connect(m_speakButton, &QPushButton::clicked, this, [this]() {
        if (!m_translatedText.isEmpty())
            TtsManager::instance().speak(m_translatedText, m_targetLang);
    });
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);

    // Phase 7: 主题/减少透明度切换时重新应用 backdrop。
    // reduce_transparency 开启时 applyAcrylicPopup 自动 no-op 走假玻璃。
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &PopupCard::applyBackdrop);
    connect(&ThemeManager::instance(), &ThemeManager::reduceTransparencyChanged,
            this, &PopupCard::applyBackdrop);

    // Phase 7-fix1：翻译字体可调。启动时应用一次，主题切换时刷新颜色
    // （跟随主题模式时 textOnGlass 会随主题换色）。
    // Phase 7-fix2：追加监听 ConfigManager::configChanged，用户在设置页改
    // result_color/result_pt 时已存在的 PopupCard 即时刷新（修复 BUG2 残留）。
    applyFontConfig();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &PopupCard::applyFontConfig);
    connect(&ConfigManager::instance(), &ConfigManager::configChanged,
            this, [this](const QString &path) {
        if (path.startsWith(QStringLiteral("ui.font.")) || path == QStringLiteral("theme"))
            applyFontConfig();
    });

    // Card styling comes from the theme QSS (QFrame#popupCard etc.).
    startTranslation();
}

void PopupCard::applyFontConfig()
{
    // Phase 7-fix1：PopupCard 译文区用 result_pt + result_color。
    // 字号直接用配置 pt（与主窗 resultEdit 同值，不按比例缩放）。
    auto &cfg = ConfigManager::instance();
    const int resPt = cfg.value("ui.font.result_pt").toInt(11);
    const QString resColorMode =
        cfg.value("ui.font.result_color").toString(QStringLiteral("theme"));

    if (m_resultLabel) {
        QFont rf = m_resultLabel->font();
        rf.setPointSize(resPt);
        m_resultLabel->setFont(rf);
        QColor color;
        if (resColorMode == QLatin1String("custom")) {
            const QString customHex =
                cfg.value("ui.font.result_color_custom").toString(QStringLiteral("#1F2937"));
            color = QColor(customHex);
            if (!color.isValid())
                color = ThemeManager::instance().palette().textOnGlass;
        } else {
            color = ThemeManager::instance().palette().textOnGlass;
        }
        // Phase 7-fix2：选择器形式仅匹配 QLabel#resultLabel 自身，风格统一。
        m_resultLabel->setStyleSheet(
            QStringLiteral("QLabel#resultLabel { color: %1; }").arg(color.name()));
    }
}

void PopupCard::popupAt(const QPoint &globalPos)
{
    adjustSize();

    QScreen *screen = QApplication::screenAt(globalPos);
    if (!screen)
        screen = QApplication::primaryScreen();
    const QRect avail = screen->availableGeometry();

    // Preferred spot: slightly below-right of the cursor; flip to the other
    // side when the card would leave the screen, then clamp.
    QPoint pos = globalPos + QPoint(10, 16);
    if (pos.x() + width() > avail.right())
        pos.setX(globalPos.x() - width() - 10);
    if (pos.y() + height() > avail.bottom())
        pos.setY(globalPos.y() - height() - 16);
    pos.setX(qBound(avail.left(), pos.x(), avail.right() - width()));
    pos.setY(qBound(avail.top(), pos.y(), avail.bottom() - height()));

    move(pos);
    show();
    raise();
    activateWindow();
    m_ready = true;
}

bool PopupCard::event(QEvent *event)
{
    // Click-elsewhere dismissal; guarded so the initial show cannot race.
    if (event->type() == QEvent::WindowDeactivate && m_ready)
        close();
    return QWidget::event(event);
}

void PopupCard::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

void PopupCard::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 首次 show 时 HWND 已创建，应用 Acrylic popup backdrop。
    // Win11 22H2+ 生效 ACCENT_ENABLE_ACRYLICBLURBEHIND；老系统/减少透明度
    // 时 no-op，qss 模板已用 glassSurface 提供假玻璃观感。
    applyBackdrop();
    startFadeIn();
}

void PopupCard::applyBackdrop()
{
#ifdef _WIN32
    QWindow *win = windowHandle();
    if (!win)
        return;
    // applyAcrylicPopup 内部读 reduce_transparency 自动 no-op；失败返回 false
    // 调用方走假玻璃（qss 模板已处理玻璃面底色）。
    WinBackdrop::applyAcrylicPopup(win);
#endif
}

void PopupCard::startFadeIn()
{
    // 玻璃淡入动效：windowOpacity 0→1，180ms ease-out。
    // 用 windowOpacity 而非 graphics effect 是为了避免与既有 QGraphicsDropShadowEffect
    // 冲突（Qt 一个 widget 只能挂一个 effect）。重复 show 时跳过动效避免闪烁。
    if (m_fadeIn)
        return;
    setWindowOpacity(0.0);
    m_fadeIn = new QPropertyAnimation(this, "windowOpacity", this);
    m_fadeIn->setDuration(180);
    m_fadeIn->setStartValue(0.0);
    m_fadeIn->setEndValue(1.0);
    m_fadeIn->setEasingCurve(QEasingCurve::OutCubic);
    // 动效结束后清理指针，允许下次 show 重新触发（如重复 popup）。
    connect(m_fadeIn, &QPropertyAnimation::finished, this, [this]() {
        m_fadeIn = nullptr;
    });
    m_fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
}

void PopupCard::startTranslation()
{
    auto *watcher = new QFutureWatcher<ManagedTransResult>(this);
    connect(watcher, &QFutureWatcher<ManagedTransResult>::finished, this,
            [this, watcher]() {
        watcher->deleteLater();
        const ManagedTransResult r = watcher->result();
        if (r.result.error.isEmpty() && !r.result.text.isEmpty()) {
            m_translatedText = r.result.text;
            if (r.result.provider == QLatin1String("mock")) {
                // 卡片已在视野内，不弹气泡，仅在译文前加 ⚠ 前缀提示服务不可达。
                m_resultLabel->setText(
                    tr("⚠ 翻译服务不可达，已显示离线占位结果\n\n%1")
                        .arg(m_translatedText));
            } else {
                m_resultLabel->setText(m_translatedText);
            }
            m_copyButton->setEnabled(true);
            m_speakButton->setEnabled(TtsManager::instance().isAvailable());

            // History scene 'selection' (skip mock fallbacks).
            if (r.result.provider != QLatin1String("mock")) {
                HistoryEntry entry;
                entry.srcLang = QStringLiteral("auto");
                entry.dstLang = m_targetLang;
                entry.srcText = m_sourceText;
                entry.dstText = m_translatedText;
                entry.provider = r.result.provider;
                entry.scene = QStringLiteral("selection");
                HistoryStore::instance().add(entry);
            }
        } else {
            m_resultLabel->setText(tr("翻译失败：%1").arg(
                r.errorChain.join(QStringLiteral(" | "))));
        }
        adjustSize();
    });
    watcher->setFuture(TranslationManager::instance().translate(
        m_sourceText, QStringLiteral("auto"), m_targetLang));
}
