#include "ui/MainWindow.h"

#include "core/autostart/AutoStartManager.h"
#include "core/capture/RegionSelector.h"
#include "core/capture/ScreenCapturer.h"
#include "core/config/ConfigManager.h"
#include "core/hotkey/HotkeyManager.h"
#include "core/ocr/OcrEngineFactory.h"
#include "core/replace/TextReplacer.h"
#include "core/selection/SelectionGrabber.h"
#include "core/tts/TtsManager.h"
#include "ui/OcrResultDialog.h"
#include "ui/LangCatalog.h"
#include "ui/history/HistorySidebar.h"
#include "ui/overlay/ScreenTranslateController.h"
#include "ui/popup/PopupCard.h"
#include "ui/settings/SettingsWindow.h"
#include "ui/theme/ThemeManager.h"
#include "ui/tray/TrayManager.h"

#ifdef _WIN32
#  include "ui/platform/WinBackdrop.h"
#endif

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCursor>
#include <QDebug>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QShowEvent>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int kDebounceMs = 600;
// v0.7.1：历史面板折叠动效时长与展开目标宽度。
constexpr int kHistoryAnimMs = 150;
constexpr int kHistoryPanelWidth = 280;

// 语言表同源（ui/LangCatalog.h）：主窗下拉与托盘"目标语言"子菜单共用。
void fillLangCombo(QComboBox *combo, bool withAuto)
{
    int n = 0;
    const LangEntry *langs = langCatalog(&n);
    for (int i = 0; i < n; ++i) {
        if (!withAuto && qstrcmp(langs[i].code, "auto") == 0)
            continue;
        combo->addItem(QCoreApplication::translate("MainWindow", langs[i].label),
                       QString::fromLatin1(langs[i].code));
    }
}

// Provider labels for the toolbar combo (mock is intentionally absent).
struct ProviderEntry { const char *key; const char *label; };
const ProviderEntry kProviders[] = {
    {"auto",    QT_TRANSLATE_NOOP("MainWindow", "自动")},
    {"google",  "Google"},
    {"bing",    "Bing"},
    {"deepl",   "DeepL"},
    {"baidu",   QT_TRANSLATE_NOOP("MainWindow", "百度翻译")},
    {"youdao",  QT_TRANSLATE_NOOP("MainWindow", "有道智云")},
    {"tencent", QT_TRANSLATE_NOOP("MainWindow", "腾讯云 TMT")},
    {"openai",  QT_TRANSLATE_NOOP("MainWindow", "OpenAI 兼容")},
    {"deeplx",  "DeepLX"},
    {"lingva",  "Lingva"},
};

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("X翻译"));
    setWindowIcon(TrayManager::appIcon());
    resize(1080, 640);
    buildUi();
    buildHistoryDock();
    initTrayAndHotkeys();
    // 启动时把注册表与 config.autostart 对齐（防第三方清理后悬空）。
    AutoStartManager::instance().alignFromConfig();

    // Phase 7: 主题/减少透明度切换时重新应用 backdrop。
    // reduce_transparency 开启时 applyTopLevel 自动 no-op，走假玻璃回退。
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &MainWindow::applyBackdrop);
    connect(&ThemeManager::instance(), &ThemeManager::reduceTransparencyChanged,
            this, &MainWindow::applyBackdrop);

    // Phase 7-fix1：翻译字体可调。启动时应用一次，配置改变即时刷新。
    // 监听 ui.font.* 路径变化（含 result_color 切换跟随主题/自选）。
    applyFontConfig();
    connect(&ConfigManager::instance(), &ConfigManager::configChanged,
            this, &MainWindow::onFontConfigChanged);
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralArea"));
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(16, 12, 16, 12);
    rootLayout->setSpacing(10);

    // ---- toolbar island (Phase 7: 玻璃岛容器包装工具条) -------------------
    // 既有控件 objectName 一律保留（swapButton/shotButton）；新增容器名
    // toolbarIsland，由 qss 模板渲染为玻璃岛样式。
    auto *toolbarIsland = new QFrame(central);
    toolbarIsland->setObjectName(QStringLiteral("toolbarIsland"));
    auto *toolRow = new QHBoxLayout(toolbarIsland);
    toolRow->setContentsMargins(10, 8, 10, 8);
    toolRow->setSpacing(8);

    m_fromCombo = new QComboBox(toolbarIsland);
    m_fromCombo->setObjectName(QStringLiteral("srcLangCombo"));
    fillLangCombo(m_fromCombo, true);
    m_fromCombo->setCurrentIndex(0); // auto

    m_swapButton = new QToolButton(toolbarIsland);
    m_swapButton->setObjectName(QStringLiteral("swapButton"));
    m_swapButton->setText(QStringLiteral("⇄"));
    m_swapButton->setToolTip(tr("交换语言"));

    m_toCombo = new QComboBox(toolbarIsland);
    m_toCombo->setObjectName(QStringLiteral("tgtLangCombo"));
    fillLangCombo(m_toCombo, false);
    // v0.7.1：目标语言从配置读（托盘子菜单同源），缺省 zh-CN。
    {
        const QString saved = ConfigManager::instance().stringValue(
            QStringLiteral("translate.target_lang"));
        const int savedIdx = m_toCombo->findData(
            saved.isEmpty() ? QStringLiteral("zh-CN") : saved);
        m_toCombo->setCurrentIndex(savedIdx >= 0 ? savedIdx : 0);
    }

    m_providerCombo = new QComboBox(toolbarIsland);
    m_providerCombo->setObjectName(QStringLiteral("providerCombo"));
    for (const ProviderEntry &e : kProviders) {
        m_providerCombo->addItem(
            QCoreApplication::translate("MainWindow", e.label),
            QString::fromLatin1(e.key));
    }

    m_shotOcrButton = new QPushButton(tr("截图OCR"), toolbarIsland);
    m_shotOcrButton->setObjectName(QStringLiteral("shotButton"));
    m_shotOcrButton->setCursor(Qt::PointingHandCursor);

    m_shotTranslateButton = new QPushButton(tr("截图翻译"), toolbarIsland);
    m_shotTranslateButton->setObjectName(QStringLiteral("shotButton"));
    m_shotTranslateButton->setCursor(Qt::PointingHandCursor);

    // v0.7.1 去菜单栏：历史/设置入口改到工具条右端图标钮（Lucide
    // history.svg / settings.svg，iconButton 玻璃胶囊样式，带 tooltip）。
    m_historyButton = new QPushButton(toolbarIsland);
    m_historyButton->setObjectName(QStringLiteral("iconButton"));
    m_historyButton->setIcon(QIcon(QStringLiteral(":/icons/history.svg")));
    m_historyButton->setIconSize(QSize(18, 18));
    m_historyButton->setAccessibleName(tr("历史记录"));
    m_historyButton->setToolTip(tr("历史记录（展开/收起）"));
    m_historyButton->setCursor(Qt::PointingHandCursor);
    m_historyButton->setFocusPolicy(Qt::NoFocus);
    m_historyButton->setFixedSize(32, 28);
    m_historyButton->setCheckable(true);

    m_settingsButton = new QPushButton(toolbarIsland);
    m_settingsButton->setObjectName(QStringLiteral("iconButton"));
    m_settingsButton->setIcon(QIcon(QStringLiteral(":/icons/settings.svg")));
    m_settingsButton->setIconSize(QSize(18, 18));
    m_settingsButton->setAccessibleName(tr("设置"));
    m_settingsButton->setToolTip(tr("设置"));
    m_settingsButton->setCursor(Qt::PointingHandCursor);
    m_settingsButton->setFocusPolicy(Qt::NoFocus);
    m_settingsButton->setFixedSize(32, 28);

    toolRow->addWidget(new QLabel(tr("源语言"), toolbarIsland));
    toolRow->addWidget(m_fromCombo);
    toolRow->addWidget(m_swapButton);
    toolRow->addWidget(new QLabel(tr("目标语言"), toolbarIsland));
    toolRow->addWidget(m_toCombo);
    toolRow->addSpacing(8);
    toolRow->addWidget(m_shotOcrButton);
    toolRow->addWidget(m_shotTranslateButton);
    toolRow->addStretch(1);
    toolRow->addWidget(new QLabel(tr("服务商"), toolbarIsland));
    toolRow->addWidget(m_providerCombo);
    toolRow->addSpacing(4);
    toolRow->addWidget(m_historyButton);
    toolRow->addWidget(m_settingsButton);
    rootLayout->addWidget(toolbarIsland);

    // Phase 7-fix2 BUG6：源语言=自动检测时的灰字提示，说明混合语种只翻第一种
    // 的限制。仅 fromCombo=auto 时可见，其余语种隐藏（避免常驻噪音）。
    m_autoLangHint = new QLabel(central);
    m_autoLangHint->setObjectName(QStringLiteral("hintLabel"));
    m_autoLangHint->setWordWrap(true);
    m_autoLangHint->setText(tr("自动检测：每次翻译仅识别源文中的主要语种；"
                               "若源文混合多种语种，将只翻译第一种，"
                               "建议手动选择源语言以获更准确结果。"));
    m_autoLangHint->setVisible(true);  // 默认 fromCombo=auto，初始可见
    rootLayout->addWidget(m_autoLangHint);

    // ---- source card (Phase 7: 玻璃卡片容器) ------------------------------
    // sourceEdit 的 objectName 保留不变；新增容器名 sourceCard。
    // 1px inset 让卡片描边可见，edit 透明背景由 qss 后代选择器控制。
    auto *sourceCard = new QFrame(central);
    sourceCard->setObjectName(QStringLiteral("sourceCard"));
    auto *sourceLayout = new QVBoxLayout(sourceCard);
    sourceLayout->setContentsMargins(1, 1, 1, 1);
    sourceLayout->setSpacing(0);

    m_sourceEdit = new QPlainTextEdit(sourceCard);
    m_sourceEdit->setObjectName(QStringLiteral("sourceEdit"));
    m_sourceEdit->setPlaceholderText(
        tr("输入要翻译的文字…（Ctrl+Enter 立即翻译）"));
    m_sourceEdit->installEventFilter(this);
    sourceLayout->addWidget(m_sourceEdit);

    // Phase 7-fix2b BUG9：清空输入图标钮（Lucide eraser），浮在原文框右上角，
    // 与 resultCard 的 copyButton 同款浮动模式。父级挂 sourceCard（同 BUG4
    // 教训：不挂 edit 下，避免样式表 color 继承链污染）。objectName=clearButton
    // 为新增名（红线允许），qss 三份主题已加同款胶囊样式。
    m_clearButton = new QPushButton(sourceCard);
    m_clearButton->setObjectName(QStringLiteral("clearButton"));
    m_clearButton->setIcon(QIcon(QStringLiteral(":/icons/eraser.svg")));
    m_clearButton->setIconSize(QSize(16, 16));
    m_clearButton->setAccessibleName(tr("清空"));
    m_clearButton->setToolTip(tr("清空输入"));
    m_clearButton->setCursor(Qt::PointingHandCursor);
    m_clearButton->setFocusPolicy(Qt::NoFocus); // 点击后焦点留在原文框
    m_clearButton->setFixedSize(28, 26);
    m_clearButton->raise();

    rootLayout->addWidget(sourceCard, 1);

    // ---- result card (Phase 7: 玻璃卡片容器) ------------------------------
    // resultEdit 的 objectName 保留不变；新增容器名 resultCard。
    // copyButton/speakButton 父级仍为 resultEdit（浮动右上角），保证
    // positionCopyButton() 逻辑零改动。
    auto *resultCard = new QFrame(central);
    resultCard->setObjectName(QStringLiteral("resultCard"));
    auto *resultLayout = new QVBoxLayout(resultCard);
    resultLayout->setContentsMargins(1, 1, 1, 1);
    resultLayout->setSpacing(0);

    m_resultEdit = new QPlainTextEdit(resultCard);
    m_resultEdit->setObjectName(QStringLiteral("resultEdit"));
    m_resultEdit->setReadOnly(true);
    m_resultEdit->setPlaceholderText(tr("译文"));
    m_resultEdit->installEventFilter(this); // reposition copy button on resize

    // Phase 7-fix2：复制/朗读按钮父级从 m_resultEdit 移到 resultCard，
    // 消除 Qt 样式表 color 沿父子继承链波及子按钮的隐患（BUG4 根治）。
    // objectName 不变（既有契约红线）。
    m_copyButton = new QPushButton(tr("复制"), resultCard);
    m_copyButton->setObjectName(QStringLiteral("copyButton"));
    m_copyButton->setCursor(Qt::PointingHandCursor);
    m_copyButton->setFixedSize(56, 26);
    m_copyButton->raise();

    m_speakButton = new QPushButton(tr("朗读"), resultCard);
    m_speakButton->setObjectName(QStringLiteral("copyButton"));
    m_speakButton->setCursor(Qt::PointingHandCursor);
    m_speakButton->setFixedSize(56, 26);
    m_speakButton->setEnabled(TtsManager::instance().isAvailable());
    m_speakButton->setToolTip(m_speakButton->isEnabled()
                                  ? tr("朗读译文")
                                  : tr("本机无可用语音引擎"));
    m_speakButton->raise();

    resultLayout->addWidget(m_resultEdit);
    rootLayout->addWidget(resultCard, 1);

    setCentralWidget(central);

    // ---- status bar --------------------------------------------------------
    m_statusLabel = new QLabel(tr("就绪"), this);
    statusBar()->addWidget(m_statusLabel, 1);

    // ---- behavior -----------------------------------------------------------
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);

    connect(m_sourceEdit, &QPlainTextEdit::textChanged,
            this, &MainWindow::onSourceTextChanged);
    connect(m_debounce, &QTimer::timeout, this, &MainWindow::triggerTranslate);
    connect(m_swapButton, &QToolButton::clicked, this, &MainWindow::swapLanguages);
    connect(m_copyButton, &QPushButton::clicked, this, &MainWindow::copyResult);
    connect(m_speakButton, &QPushButton::clicked, this, &MainWindow::speakResult);
    // Phase 7-fix2b BUG9：清空输入
    connect(m_clearButton, &QPushButton::clicked, this, &MainWindow::clearInput);
    connect(m_shotOcrButton, &QPushButton::clicked,
            this, &MainWindow::startScreenshotOcr);
    connect(m_shotTranslateButton, &QPushButton::clicked,
            this, &MainWindow::startScreenshotTranslate);
    // v0.7.1：工具条双入口（去菜单栏后历史/设置仍可达）。
    connect(m_historyButton, &QPushButton::clicked,
            this, &MainWindow::toggleHistoryPanel);
    connect(m_settingsButton, &QPushButton::clicked,
            this, &MainWindow::openSettings);
    // v0.7.1：目标语言 → 配置（与托盘子菜单双向同步；写前比对防回环）。
    connect(m_toCombo, &QComboBox::currentIndexChanged, this, [this]() {
        const QString code = m_toCombo->currentData().toString();
        ConfigManager &cfg = ConfigManager::instance();
        if (!code.isEmpty()
            && cfg.stringValue(QStringLiteral("translate.target_lang")) != code)
            cfg.setValue(QStringLiteral("translate.target_lang"), code);
    });
    // 配置 → 目标语言下拉（托盘切语言即时联动；同值早退防回环）。
    connect(&ConfigManager::instance(), &ConfigManager::configChanged,
            this, [this](const QString &path) {
        if (path != QLatin1String("translate.target_lang") || !m_toCombo)
            return;
        const QString code = ConfigManager::instance().stringValue(
            QStringLiteral("translate.target_lang"));
        if (code.isEmpty() || m_toCombo->currentData().toString() == code)
            return;
        const int idx = m_toCombo->findData(code);
        if (idx >= 0)
            m_toCombo->setCurrentIndex(idx);
    });
    // Phase 7-fix2 BUG6：源语言切换时刷新 hint 可见性
    connect(m_fromCombo, &QComboBox::currentIndexChanged,
            this, [this](int idx) {
        if (!m_autoLangHint)
            return;
        const QString code = m_fromCombo->itemData(idx).toString();
        m_autoLangHint->setVisible(code == QLatin1String("auto"));
    });
    positionCopyButton();
    positionClearButton();
    m_sourceEdit->setFocus();
}

void MainWindow::buildHistoryDock()
{
    m_historyDock = new HistorySidebar(this);
    addDockWidget(Qt::RightDockWidgetArea, m_historyDock);
    connect(m_historyDock, &HistorySidebar::entryActivated,
            this, &MainWindow::onHistoryEntryActivated);

    // v0.7.1 去菜单栏：开关改由工具条"历史"图标钮控制（toggleHistoryPanel，
    // 150ms 宽度动效）。dock 自身标题栏关闭/浮动等可见性变化同步回按钮
    // 勾选态，保证两侧状态一致。
    connect(m_historyDock, &QDockWidget::visibilityChanged,
            this, [this](bool visible) {
        if (m_historyButton)
            m_historyButton->setChecked(visible);
    });
    if (m_historyButton)
        m_historyButton->setChecked(m_historyDock->isVisible());
}

void MainWindow::toggleHistoryPanel()
{
    if (!m_historyDock)
        return;
    // 重入保护：快速连点时先停掉上一段动画并恢复自由宽度。
    if (m_historyAnim) {
        m_historyAnim->stop();
        m_historyAnim = nullptr;
        m_historyDock->setMaximumWidth(QWIDGETSIZE_MAX);
    }
    const bool show = !m_historyDock->isVisible();
    auto *anim = new QPropertyAnimation(m_historyDock,
                                        QByteArrayLiteral("maximumWidth"),
                                        this);
    m_historyAnim = anim;
    anim->setDuration(kHistoryAnimMs);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    if (show) {
        m_historyDock->setMaximumWidth(0);
        m_historyDock->setVisible(true);
        anim->setStartValue(0);
        anim->setEndValue(kHistoryPanelWidth);
    } else {
        anim->setStartValue(m_historyDock->width());
        anim->setEndValue(0);
    }
    connect(anim, &QPropertyAnimation::finished, this, [this, anim, show]() {
        if (!show)
            m_historyDock->setVisible(false);
        // 动效结束后解除宽度锁，恢复用户自由拖宽 dock。
        m_historyDock->setMaximumWidth(QWIDGETSIZE_MAX);
        if (m_historyAnim == anim)
            m_historyAnim = nullptr;
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::openSettings()
{
    SettingsWindow::open(this);
}

void MainWindow::onHistoryEntryActivated(const HistoryEntry &entry)
{
    // Refill languages when the entry carries them, then re-run the request.
    const int fromIdx = m_fromCombo->findData(entry.srcLang);
    if (fromIdx >= 0)
        m_fromCombo->setCurrentIndex(fromIdx);
    const int toIdx = m_toCombo->findData(entry.dstLang);
    if (toIdx >= 0)
        m_toCombo->setCurrentIndex(toIdx);
    m_sourceEdit->setPlainText(entry.srcText);
    showNormal();
    raise();
    activateWindow();
    m_debounce->stop();
    triggerTranslate();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_sourceEdit && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
            && ke->modifiers().testFlag(Qt::ControlModifier)) {
            m_debounce->stop();
            triggerTranslate();
            return true;
        }
        // Phase 7-fix2b BUG9：原文框聚焦时 Esc = 清空输入。主窗内无其它 Esc
        // 消费者（PopupCard/RegionSelector 的 Esc 在各自独立窗口，互不冲突；
        // QPlainTextEdit 默认忽略 Esc），可安全占用。
        if (ke->key() == Qt::Key_Escape && ke->modifiers() == Qt::NoModifier) {
            clearInput();
            return true;
        }
    }
    // Phase 7-fix2b BUG9：sourceEdit resize 时重定位浮动清空钮。
    if (watched == m_sourceEdit && event->type() == QEvent::Resize)
        positionClearButton();
    if (watched == m_resultEdit && event->type() == QEvent::Resize)
        positionCopyButton();
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    positionCopyButton();
    positionClearButton();
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    // 首次 show 时 HWND 已创建，应用 DWM backdrop。
    // Win11 22H2+ 生效 Mica/Acrylic，老系统/减少透明度时 no-op 走假玻璃。
    applyBackdrop();
}

void MainWindow::positionCopyButton()
{
    if (!m_copyButton || !m_resultEdit)
        return;
    // Phase 7-fix2：按钮父级已移到 resultCard，坐标基于 m_resultEdit 在
    // resultCard 内的 geometry（相对父级）计算右上角。resultCard margins(1,1,1,1)
    // 使得 editRect.right() 与旧 m_resultEdit->width() 数值一致，视觉零漂移。
    const QRect editRect = m_resultEdit->geometry();
    m_copyButton->move(editRect.right() - m_copyButton->width() - 12,
                       editRect.top() + 8);
    if (m_speakButton) {
        m_speakButton->move(editRect.right() - m_copyButton->width()
                                - m_speakButton->width() - 18,
                            editRect.top() + 8);
    }
}

void MainWindow::positionClearButton()
{
    if (!m_clearButton || !m_sourceEdit)
        return;
    // Phase 7-fix2b BUG9：与 positionCopyButton 同款——父级 sourceCard，
    // 按 sourceEdit 在卡片内的 geometry 定位右上角，避开滚动条留 12px。
    const QRect editRect = m_sourceEdit->geometry();
    m_clearButton->move(editRect.right() - m_clearButton->width() - 12,
                        editRect.top() + 8);
}

void MainWindow::clearInput()
{
    // Phase 7-fix2b BUG9：一键清空原文+译文+状态栏复位，焦点回原文框。
    // 约束：只清当前输入/译文——不动历史记录、不动配置、不触发新翻译。
    // sourceEdit->clear() 会发 textChanged 启动防抖计时器，随后 stop 掐掉，
    // 保证不会在 600ms 后走 triggerTranslate（即使走到，空文本分支也无网络
    // 请求）。原文已空时各步均幂等，无副作用不报错。
    m_sourceEdit->clear();
    m_debounce->stop();
    ++m_requestSeq; // 使在途翻译结果失效，防止清空后译文又被回填
    m_resultEdit->clear();
    m_statusLabel->setText(tr("就绪"));
    m_sourceEdit->setFocus();
}

void MainWindow::applyBackdrop()
{
#ifdef _WIN32
    QWindow *win = windowHandle();
    if (!win)
        return;
    const bool dark = ThemeManager::instance().isDark();
    // applyTopLevel 内部会读 reduce_transparency 自动 no-op，
    // 失败时返回 false（调用方走假玻璃，qss 模板已处理）。
    const bool ok = WinBackdrop::applyTopLevel(win, dark);
    if (ok) {
        // 反读 backdrop 类型作为生效证明（任务 D 节）。
        const int queried = WinBackdrop::queryBackdrop(win);
        qInfo().noquote() << QStringLiteral(
            "[backdrop] MainWindow queryBackdrop=%1 (2=Mica,3=Acrylic)")
            .arg(queried);
    }
#else
    // 非 Windows 平台无 DWM backdrop，走假玻璃（qss 已处理）。
#endif
}

void MainWindow::applyFontConfig()
{
    // Phase 7-fix1：从 ConfigManager 读 ui.font 应用到 sourceEdit/resultEdit。
    // 字号直接用配置 pt；颜色 resultEdit 用 result_color（theme 或 custom），
    // sourceEdit 始终跟随主题 text（原文非阅读焦点，颜色不需要自定义）。
    auto &cfg = ConfigManager::instance();
    const int srcPt = cfg.value("ui.font.source_pt").toInt(11);
    const int resPt = cfg.value("ui.font.result_pt").toInt(11);
    const QString resColorMode =
        cfg.value("ui.font.result_color").toString(QStringLiteral("theme"));

    if (m_sourceEdit) {
        QFont sf = m_sourceEdit->font();
        sf.setPointSize(srcPt);
        m_sourceEdit->setFont(sf);
    }
    if (m_resultEdit) {
        QFont rf = m_resultEdit->font();
        rf.setPointSize(resPt);
        m_resultEdit->setFont(rf);
        // 颜色通过 setStyleSheet 覆盖 QSS color，仅 resultEdit 受影响
        // （resultCard 父级仍由 QSS 卡片样式控制，编辑区前景色独立设置）。
        QColor color;
        if (resColorMode == QLatin1String("custom")) {
            const QString customHex =
                cfg.value("ui.font.result_color_custom").toString(QStringLiteral("#1F2937"));
            color = QColor(customHex);
            if (!color.isValid())
                color = ThemeManager::instance().palette().textOnGlass;
        } else {
            // 跟随主题：用 textOnGlass 保证 ≥4.5:1 对比度红线
            color = ThemeManager::instance().palette().textOnGlass;
        }
        // Phase 7-fix2：用选择器形式仅匹配 QPlainTextEdit#resultEdit 自身，
        // 不波及子控件（BUG4）；与 QSS 后代选择器 QFrame#resultCard QPlainTextEdit
        // 优先级对等，widget 本地 styleSheet 胜出（BUG2 根治）。
        // Qt QSS 不支持 !important（CSS3 特性），选择器形式才是正解。
        m_resultEdit->setStyleSheet(
            QStringLiteral("QPlainTextEdit#resultEdit { color: %1; }")
                .arg(color.name()));
    }
}

void MainWindow::onFontConfigChanged(const QString &path)
{
    // Phase 7-fix1：监听 ui.font.* 与 theme 变化即时刷新字体
    if (path == QLatin1String("ui.font.source_pt")
        || path == QLatin1String("ui.font.result_pt")
        || path == QLatin1String("ui.font.result_color")
        || path == QLatin1String("ui.font.result_color_custom")
        || path == QLatin1String("theme")) {
        applyFontConfig();
    }
}

void MainWindow::onSourceTextChanged()
{
    m_debounce->start(); // restart -> translate 600ms after typing stops
}

QString MainWindow::currentProviderKey() const
{
    return m_providerCombo->currentData().toString();
}

void MainWindow::triggerTranslate()
{
    const QString text = m_sourceEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        m_resultEdit->clear();
        m_statusLabel->setText(tr("就绪"));
        return;
    }

    const QString from = m_fromCombo->currentData().toString();
    const QString to   = m_toCombo->currentData().toString();
    const quint64 seq  = ++m_requestSeq;

    m_statusLabel->setText(tr("翻译中…"));

    auto *watcher = new QFutureWatcher<ManagedTransResult>(this);
    connect(watcher, &QFutureWatcher<ManagedTransResult>::finished, this,
            [this, watcher, seq, text, from, to]() {
        watcher->deleteLater();
        if (seq != m_requestSeq)
            return; // superseded by a newer request
        const ManagedTransResult r = watcher->result();
        if (r.result.error.isEmpty()) {
            m_resultEdit->setPlainText(r.result.text);
            QString status = QStringLiteral("%1 · %2 ms")
                                 .arg(r.result.provider)
                                 .arg(r.result.elapsedMs);
            if (r.result.provider == QLatin1String("mock")) {
                // 落到 mock 兜底：服务不可达，结果为离线占位译文。
                // 状态栏加 ⚠ 前缀，tooltip 显示完整 error_chain 方便用户自查网络。
                status = tr("⚠ 翻译服务不可达，已显示离线占位结果");
                m_statusLabel->setToolTip(r.errorChain.join(QStringLiteral(" | ")));
            } else {
                if (!r.errorChain.isEmpty())
                    status += tr("（已降级：%1）")
                                  .arg(r.errorChain.join(QStringLiteral(" | ")));
                m_statusLabel->setToolTip(QString());
            }
            m_statusLabel->setText(status);

            // History scene 'input' (mock fallbacks are not worth keeping).
            if (r.result.provider != QLatin1String("mock")) {
                HistoryEntry entry;
                entry.srcLang = from;
                entry.dstLang = to;
                entry.srcText = text;
                entry.dstText = r.result.text;
                entry.provider = r.result.provider;
                entry.scene = QStringLiteral("input");
                HistoryStore::instance().add(entry);
            }
        } else {
            m_resultEdit->clear();
            m_statusLabel->setText(tr("翻译失败：%1")
                                       .arg(r.errorChain.join(QStringLiteral(" | "))));
            // 失败时 tooltip 显示完整 error_chain 供用户诊断网络。
            m_statusLabel->setToolTip(r.errorChain.join(QStringLiteral(" | ")));
        }
    });
    watcher->setFuture(TranslationManager::instance().translate(
        text, from, to, currentProviderKey()));
}

void MainWindow::swapLanguages()
{
    // Exchange language selections ("auto" swaps to target-less sensible value).
    const QString fromCode = m_fromCombo->currentData().toString();
    const QString toCode   = m_toCombo->currentData().toString();

    const int newFrom = m_fromCombo->findData(toCode);
    if (newFrom >= 0)
        m_fromCombo->setCurrentIndex(newFrom);
    if (fromCode != QLatin1String("auto")) {
        const int newTo = m_toCombo->findData(fromCode);
        if (newTo >= 0)
            m_toCombo->setCurrentIndex(newTo);
    }

    // Move the previous translation back into the source box.
    const QString translated = m_resultEdit->toPlainText();
    if (!translated.isEmpty()) {
        m_sourceEdit->setPlainText(translated);
        m_resultEdit->clear();
    }
}

void MainWindow::copyResult()
{
    const QString text = m_resultEdit->toPlainText();
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
        m_statusLabel->setText(tr("已复制到剪贴板"));
    }
}

void MainWindow::speakResult()
{
    const QString text = m_resultEdit->toPlainText();
    if (text.isEmpty())
        return;
    // v0.7.1 BUG-A：译文语种就是当前目标语言（dstLang），直接传它选嗓音；
    // 不再走 guessLang 启发式（日/韩译文曾被归到 zh 导致选错嗓音）。
    TtsManager::instance().speak(text, m_toCombo->currentData().toString());
    m_statusLabel->setText(tr("朗读中…"));
}

void MainWindow::initTrayAndHotkeys()
{
    // ---- tray -------------------------------------------------------------
    m_tray = new TrayManager(this);
    connect(m_tray, &TrayManager::screenshotTranslateRequested,
            this, &MainWindow::startScreenshotTranslate);
    connect(m_tray, &TrayManager::screenshotOcrRequested,
            this, &MainWindow::startScreenshotOcr);
    connect(m_tray, &TrayManager::selectionTranslateRequested,
            this, &MainWindow::startSelectionTranslate);
    connect(m_tray, &TrayManager::toggleMainWindowRequested,
            this, &MainWindow::toggleVisibility);
    connect(m_tray, &TrayManager::speakClipboardRequested,
            this, &MainWindow::speakClipboard);
    connect(m_tray, &TrayManager::settingsRequested,
            this, &MainWindow::openSettings);
    connect(m_tray, &TrayManager::quitRequested,
            this, &MainWindow::quitApplication);

    // TTS engine missing: one bubble, buttons stay greyed out.
    connect(&TtsManager::instance(), &TtsManager::unavailable, this,
            [this](const QString &message) {
        m_tray->showBubble(QStringLiteral("X翻译"), message);
    });

    // ---- global hotkeys -----------------------------------------------------
    // Bindings come from the config (settings page persists rebinds there);
    // the phase-3 defaults are the fallback for missing keys.
    ConfigManager &cfg = ConfigManager::instance();
    HotkeyManager &hotkeys = HotkeyManager::instance();
    const auto callbackFor = [this](const QString &actionId) {
        return [this, actionId]() {
            if (actionId == QLatin1String("screenshot_translate"))
                startScreenshotTranslate();
            else if (actionId == QLatin1String("screenshot_ocr"))
                startScreenshotOcr();
            else if (actionId == QLatin1String("selection_translate"))
                startSelectionTranslate();
            else if (actionId == QLatin1String("toggle_main"))
                toggleVisibility();
            else if (actionId == QLatin1String("speak_clipboard"))
                speakClipboard();
            else if (actionId == QLatin1String("text_replace"))
                startTextReplace();
        };
    };
    const QVector<HotkeyManager::ActionSpec> specs = HotkeyManager::defaultActions();
    for (const HotkeyManager::ActionSpec &spec : specs) {
        const QString saved = cfg.hotkeyFor(spec.actionId);
        const QKeySequence seq = saved.isEmpty() ? spec.sequence
                                                 : QKeySequence(saved);
        if (!hotkeys.registerAction(spec.actionId, seq,
                                    callbackFor(spec.actionId))) {
            // v0.7.2：冲突不再默认弹气泡（Alt+R 被占每次启动都弹，骚扰），
            // 改为恒写 qWarning 日志；气泡受 notifications.hotkey_conflict
            // 类目开关控制（默认关，设置→常规→通知可开）。注册仍继续，
            // 其余热键不受影响。
            qWarning().noquote() << QStringLiteral(
                "[hotkey] conflict: %1 (%2) already taken by another process, "
                "action disabled; rebind in settings")
                .arg(seq.toString(QKeySequence::NativeText), spec.label);
            m_tray->showBubble(
                QStringLiteral("X翻译"),
                tr("快捷键 %1 被占用（%2 不可用），可在设置中修改")
                    .arg(seq.toString(QKeySequence::NativeText), spec.label),
                QStringLiteral("hotkey_conflict"));
        }
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Closing hides to the tray; only 退出 in the tray menu really quits.
    if (!m_quitting && m_tray && m_tray->isAvailable()) {
        event->ignore();
        hide();
        // v0.7.2："已最小化到托盘"改为配置标记 tray.minimized_hint_shown，
        // 全局仅首次弹一次（跨启动持久）；旧实现是进程内标记，每次
        // 启动后首次关窗都会弹。
        ConfigManager &cfg = ConfigManager::instance();
        if (!cfg.boolValue(QStringLiteral("tray.minimized_hint_shown"))) {
            cfg.setValue(QStringLiteral("tray.minimized_hint_shown"), true);
            m_tray->showBubble(
                QStringLiteral("X翻译"),
                tr("已最小化到托盘，双击图标恢复；托盘菜单可退出"));
        }
        return;
    }
    event->accept();
}

void MainWindow::startSelectionTranslate()
{
    if (!ConfigManager::instance().selectionEnabled()) {
        m_tray->showBubble(QStringLiteral("X翻译"),
                           tr("划词翻译已在设置中关闭"),
                           QStringLiteral("selection"));
        return;
    }
    auto *grabber = new SelectionGrabber(this);
    connect(grabber, &SelectionGrabber::finished, this,
            [this, grabber](const SelectionResult &result) {
        grabber->deleteLater();
        // stderr statistics line for the dev report (source route tally).
        qInfo().noquote() << QStringLiteral(
            "[selection] source=%1 len=%2 latency=%3ms restored=%4")
            .arg(result.source.isEmpty() ? QStringLiteral("none") : result.source)
            .arg(result.text.size())
            .arg(result.copyLatencyMs)
            .arg(result.clipboardRestored);
        if (result.text.trimmed().isEmpty()) {
            m_tray->showBubble(QStringLiteral("X翻译"),
                               tr("未检测到选中文本"),
                               QStringLiteral("selection"));
            return;
        }
        auto *card = new PopupCard(result.text.trimmed());
        card->popupAt(QCursor::pos());
    });
    grabber->start();
}

void MainWindow::startTextReplace()
{
    // 文本替换热键不依赖 selection.enabled 开关（语义不同：替换是独立动作）。
    const QString from = m_fromCombo ? m_fromCombo->currentData().toString()
                                     : QStringLiteral("auto");
    const QString to = m_toCombo ? m_toCombo->currentData().toString()
                                 : QStringLiteral("zh-CN");
    const QString provider = currentProviderKey();
    auto *replacer = new TextReplacer(this);
    connect(replacer, &TextReplacer::finished, this,
            [this, replacer, from, to](const ReplaceResult &result) {
        replacer->deleteLater();
        // stderr 统计行：用于 dev 报告核对替换链路命中。
        qInfo().noquote() << QStringLiteral(
            "[replace] provider=%1 replaced=%2 restored=%3 err=%4")
            .arg(result.provider)
            .arg(result.replaced)
            .arg(result.clipboardRestored)
            .arg(result.error.isEmpty() ? QStringLiteral("none") : result.error);
        if (result.error.isEmpty() && result.replaced) {
            if (result.provider == QLatin1String("mock")) {
                // 后台替换落到 mock：用户可能没在看窗口，弹气泡提示并引导到代理设置。
                m_statusLabel->setText(
                    tr("⚠ 已用离线占位结果替换（翻译服务不可达）"));
                m_statusLabel->setToolTip(result.error);
                m_tray->showBubble(
                    QStringLiteral("X翻译"),
                    tr("⚠ 翻译服务不可达，已用离线占位结果替换 — "
                       "可在 设置→代理→系统代理 中检查"),
                    QStringLiteral("translate_failed"));
            } else {
                m_statusLabel->setText(
                    tr("已替换为译文（%1）").arg(result.provider));
                m_statusLabel->setToolTip(QString());
            }
            // 历史记录 scene='replace'，mock 兜底不写。
            if (result.provider != QLatin1String("mock")) {
                HistoryEntry entry;
                entry.srcLang = from;
                entry.dstLang = to;
                entry.srcText = result.originalText;
                entry.dstText = result.translatedText;
                entry.provider = result.provider;
                entry.scene = QStringLiteral("replace");
                HistoryStore::instance().add(entry);
            }
        } else if (!result.error.isEmpty()) {
            m_tray->showBubble(QStringLiteral("X翻译"),
                               tr("替换失败：%1").arg(result.error),
                               QStringLiteral("replace"));
        } else if (result.originalText.trimmed().isEmpty()) {
            m_tray->showBubble(QStringLiteral("X翻译"),
                               tr("未检测到选中文本"),
                               QStringLiteral("replace"));
        }
    });
    replacer->start(from, to, provider);
}

void MainWindow::toggleVisibility()
{
    if (isVisible() && !isMinimized()) {
        hide();
    } else {
        showNormal();
        raise();
        activateWindow();
    }
}

void MainWindow::speakClipboard()
{
    const QString text = QApplication::clipboard()->text().trimmed();
    if (text.isEmpty()) {
        m_tray->showBubble(QStringLiteral("X翻译"),
                           tr("剪贴板没有可朗读的文本"));
        return;
    }
    TtsManager::instance().speak(text, TtsManager::guessLang(text));
}

void MainWindow::quitApplication()
{
    m_quitting = true;
    HotkeyManager::instance().unregisterAll();
    TtsManager::instance().stop();
    // Overlays/ControlBars are parented to this window through their
    // ScreenTranslateController and die with it when the app quits.
    close();
    qApp->quit();
}

void MainWindow::startScreenshotOcr()
{
    auto *session = new RegionSelectSession(this, this);
    connect(session, &RegionSelectSession::regionSelected,
            this, &MainWindow::runOcrOnRegion);
    connect(session, &RegionSelectSession::cancelled, this, [this]() {
        m_statusLabel->setText(tr("已取消选区"));
    });
    session->start();
}

void MainWindow::runOcrOnRegion(const QRect &globalLogicalRect)
{
    const QImage frame = ScreenCapturer::grabLogicalRect(globalLogicalRect);
    if (frame.isNull()) {
        m_statusLabel->setText(tr("截屏失败"));
        return;
    }
    m_statusLabel->setText(tr("识别中…"));

    auto *watcher = new QFutureWatcher<OcrResult>(this);
    connect(watcher, &QFutureWatcher<OcrResult>::finished, this,
            [this, watcher]() {
        watcher->deleteLater();
        const OcrResult result = watcher->result();
        if (!result.error.isEmpty()) {
            m_statusLabel->setText(tr("OCR 失败:%1").arg(result.error));
            return;
        }
        QStringList lines;
        for (const OcrLine &line : result.lines)
            lines.append(line.text);
        m_statusLabel->setText(tr("识别完成:%1 行 · %2 ms")
                                   .arg(lines.size())
                                   .arg(result.elapsedMs));

        auto *dialog = new OcrResultDialog(lines.join(QLatin1Char('\n')), this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog, &OcrResultDialog::translateRequested, this,
                [this](const QString &text) {
            // Send back to the source box; textChanged kicks the debounce,
            // trigger immediately for snappier feedback.
            m_sourceEdit->setPlainText(text);
            m_debounce->stop();
            triggerTranslate();
        });
        dialog->show();
    });
    watcher->setFuture(OcrEngineFactory::instance().engine()->recognize(frame));
}

void MainWindow::startScreenshotTranslate()
{
    auto *session = new RegionSelectSession(this, this);
    connect(session, &RegionSelectSession::regionSelected, this,
            [this](const QRect &region) {
        auto *controller = new ScreenTranslateController(
            region, m_fromCombo->currentData().toString(),
            m_toCombo->currentData().toString(), currentProviderKey(), this);
        connect(controller, &ScreenTranslateController::statusMessage,
                m_statusLabel, &QLabel::setText);
        // 截图翻译落到 mock 时弹托盘气泡，受 translate_failed 类目开关控制。
        connect(controller, &ScreenTranslateController::fallbackToMock,
                this, [this](const QString &msg) {
            m_tray->showBubble(QStringLiteral("X翻译"), msg,
                               QStringLiteral("translate_failed"));
        });
        controller->start();
    });
    connect(session, &RegionSelectSession::cancelled, this, [this]() {
        m_statusLabel->setText(tr("已取消选区"));
    });
    session->start();
}
