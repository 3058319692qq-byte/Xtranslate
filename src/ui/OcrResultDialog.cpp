#include "ui/OcrResultDialog.h"

#include "core/config/ConfigManager.h"
#include "core/tts/TtsManager.h"
#include "ui/theme/ThemeManager.h"

#ifdef _WIN32
#  include "ui/platform/WinBackdrop.h"
#endif

#include <QApplication>
#include <QClipboard>
#include <QFont>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QSize>
#include <QVBoxLayout>

namespace {

// Phase 7: 图标按钮工厂——objectName=iconButton 走 qss 模板的玻璃胶囊样式。
// accessibleName 保留原中文文案给 UIA / 屏幕阅读器（红线：可达性名字不删）。
// OcrResultDialog 与按钮本身此前均无 objectName；新增 iconButton 名字。
QPushButton *makeIconButton(const QString &iconRes, const QString &accessibleName,
                            bool primary, QWidget *parent)
{
    auto *btn = new QPushButton(parent);
    btn->setObjectName(primary ? QStringLiteral("primaryIconButton")
                               : QStringLiteral("iconButton"));
    btn->setIcon(QIcon(iconRes));
    btn->setIconSize(QSize(18, 18));
    btn->setAccessibleName(accessibleName);
    btn->setToolTip(accessibleName);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedSize(36, 30);
    return btn;
}

} // namespace

OcrResultDialog::OcrResultDialog(const QString &text, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("识别结果 - X翻译"));
    resize(520, 380);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 12);
    layout->setSpacing(10);

    m_edit = new QPlainTextEdit(this);
    m_edit->setPlainText(text);
    layout->addWidget(m_edit, 1);

    // Phase 7: 图标化按钮（copy/speak/replace/close）。
    // primaryIconButton 用于"翻译"主操作，qss 模板未单独定义时回落到默认 QPushButton
    // 样式（实心橙 + 白字），与既有视觉一致。
    auto *buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(8);
    auto *copyBtn = makeIconButton(QStringLiteral(":/icons/copy.svg"),
                                   tr("复制全部"), false, this);
    auto *speakBtn = makeIconButton(QStringLiteral(":/icons/speak.svg"),
                                    tr("朗读"), false, this);
    auto *translateBtn = makeIconButton(QStringLiteral(":/icons/replace.svg"),
                                        tr("翻译"), true, this);
    auto *closeBtn = makeIconButton(QStringLiteral(":/icons/close.svg"),
                                    tr("关闭"), false, this);
    speakBtn->setEnabled(TtsManager::instance().isAvailable());
    if (!speakBtn->isEnabled())
        speakBtn->setToolTip(tr("本机无可用语音引擎"));
    buttonRow->addStretch(1);
    buttonRow->addWidget(copyBtn);
    buttonRow->addWidget(speakBtn);
    buttonRow->addWidget(translateBtn);
    buttonRow->addWidget(closeBtn);
    layout->addLayout(buttonRow);

    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_edit->toPlainText());
    });
    connect(speakBtn, &QPushButton::clicked, this, [this]() {
        const QString current = m_edit->toPlainText();
        TtsManager::instance().speak(current, TtsManager::guessLang(current));
    });
    connect(translateBtn, &QPushButton::clicked, this, [this]() {
        emit translateRequested(m_edit->toPlainText());
        accept();
    });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    // Phase 7: 主题/减少透明度切换时重新应用 backdrop。
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &OcrResultDialog::applyBackdrop);
    // Phase 7-fix1：翻译字体可调。启动时应用一次，主题切换刷新颜色。
    // Phase 7-fix2：追加监听 ConfigManager::configChanged，用户在设置页改
    // result_color/result_pt 时已打开的 OcrResultDialog 即时刷新（修复 BUG2）。
    applyFontConfig();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &OcrResultDialog::applyFontConfig);
    connect(&ThemeManager::instance(), &ThemeManager::reduceTransparencyChanged,
            this, &OcrResultDialog::applyBackdrop);
    connect(&ConfigManager::instance(), &ConfigManager::configChanged,
            this, [this](const QString &path) {
        if (path.startsWith(QStringLiteral("ui.font.")) || path == QStringLiteral("theme"))
            applyFontConfig();
    });

    // Styling comes from the theme QSS.
}

QString OcrResultDialog::currentText() const
{
    return m_edit->toPlainText();
}

void OcrResultDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    // 首次 show 时 HWND 已创建，应用 DWM backdrop（Mica/Acrylic 由常量决定）。
    applyBackdrop();
}

void OcrResultDialog::applyBackdrop()
{
#ifdef _WIN32
    QWindow *win = windowHandle();
    if (!win)
        return;
    const bool dark = ThemeManager::instance().isDark();
    const bool ok = WinBackdrop::applyTopLevel(win, dark);
    if (ok) {
        const int queried = WinBackdrop::queryBackdrop(win);
        qInfo().noquote() << QStringLiteral(
            "[backdrop] OcrResultDialog queryBackdrop=%1 (2=Mica,3=Acrylic)")
            .arg(queried);
    }
#else
    // 非 Windows 平台无 DWM backdrop，走假玻璃（qss 已处理）。
#endif
}

void OcrResultDialog::applyFontConfig()
{
    // Phase 7-fix1：OcrResultDialog 文本区用 result_pt + result_color。
    // 字号直接用配置 pt（与主窗 resultEdit 同值）。
    auto &cfg = ConfigManager::instance();
    const int resPt = cfg.value("ui.font.result_pt").toInt(11);
    const QString resColorMode =
        cfg.value("ui.font.result_color").toString(QStringLiteral("theme"));

    if (m_edit) {
        QFont rf = m_edit->font();
        rf.setPointSize(resPt);
        m_edit->setFont(rf);
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
        // Phase 7-fix2：选择器形式仅匹配 QPlainTextEdit 自身（m_edit 无 objectName，
        // 用类型选择器；无子控件，安全不波及）。
        m_edit->setStyleSheet(
            QStringLiteral("QPlainTextEdit { color: %1; }").arg(color.name()));
    }
}
