#include "ui/settings/SettingsWindow.h"

#include "core/autostart/AutoStartManager.h"
#include "core/config/ConfigManager.h"
#include "core/hotkey/HotkeyManager.h"
#include "core/ocr/OcrEngineFactory.h"
#include "core/ocr/SystemOcrEngine.h"
#include "core/plugin/PluginManager.h"
#include "core/translate/TranslationManager.h"
#include "core/tts/EdgeTtsProvider.h"
#include "core/tts/TtsManager.h"
#include "ui/LangCatalog.h"
#include "ui/theme/ThemeManager.h"

#ifdef _WIN32
#  include "ui/platform/WinBackdrop.h"
#endif

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QShowEvent>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QPointer<SettingsWindow> g_instance;

// Field descriptors for the provider config forms.
struct FieldSpec {
    const char *key;      // JSON field under providers.<name>
    const char *label;    // form label (translated at build time)
    bool secret;          // password echo mode
};

QList<FieldSpec> fieldsFor(const QString &provider)
{
    if (provider == QLatin1String("deepl"))
        return {{"apiKey", QT_TRANSLATE_NOOP("SettingsWindow", "Auth Key"), true}};
    if (provider == QLatin1String("baidu"))
        return {{"appId", QT_TRANSLATE_NOOP("SettingsWindow", "APP ID"), false},
                {"apiKey", QT_TRANSLATE_NOOP("SettingsWindow", "密钥"), true}};
    if (provider == QLatin1String("youdao"))
        return {{"appKey", QT_TRANSLATE_NOOP("SettingsWindow", "应用 ID"), false},
                {"appSecret", QT_TRANSLATE_NOOP("SettingsWindow", "应用密钥"), true}};
    if (provider == QLatin1String("tencent"))
        return {{"secretId", QT_TRANSLATE_NOOP("SettingsWindow", "SecretId"), false},
                {"secretKey", QT_TRANSLATE_NOOP("SettingsWindow", "SecretKey"), true},
                {"region", QT_TRANSLATE_NOOP("SettingsWindow", "地域"), false}};
    if (provider == QLatin1String("openai"))
        return {{"baseUrl", QT_TRANSLATE_NOOP("SettingsWindow", "接口地址"), false},
                {"apiKey", QT_TRANSLATE_NOOP("SettingsWindow", "API Key"), true},
                {"model", QT_TRANSLATE_NOOP("SettingsWindow", "模型"), false}};
    if (provider == QLatin1String("zhipu"))
        return {{"apiKey", QT_TRANSLATE_NOOP("SettingsWindow", "API Key"), true},
                {"baseUrl", QT_TRANSLATE_NOOP("SettingsWindow", "接口地址"), false},
                {"model", QT_TRANSLATE_NOOP("SettingsWindow", "模型"), false}};
    if (provider == QLatin1String("deeplx"))
        return {{"baseUrl", QT_TRANSLATE_NOOP("SettingsWindow", "接口地址"), false}};
    return {}; // google / bing / mymemory: key-free
}

QLabel *makePageTitle(const QString &text, QWidget *parent)
{
    auto *title = new QLabel(text, parent);
    title->setObjectName(QStringLiteral("pageTitle"));
    return title;
}

QLabel *makeHint(const QString &text, QWidget *parent)
{
    auto *hint = new QLabel(text, parent);
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    return hint;
}

} // namespace

void SettingsWindow::open(QWidget *parent)
{
    if (!g_instance)
        g_instance = new SettingsWindow(parent);
    g_instance->show();
    g_instance->raise();
    g_instance->activateWindow();
}

SettingsWindow::SettingsWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("设置 - X翻译"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(860, 600);

    auto *rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_nav = new QListWidget(this);
    m_nav->setObjectName(QStringLiteral("settingsNav"));
    m_nav->setFixedWidth(150);
    m_nav->addItems({tr("常规"), tr("快捷键"), tr("翻译服务"), tr("OCR"),
                     tr("朗读"), tr("代理"), tr("插件"), tr("关于")});

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildGeneralPage());
    m_stack->addWidget(buildHotkeyPage());
    m_stack->addWidget(buildProvidersPage());
    m_stack->addWidget(buildOcrPage());
    m_stack->addWidget(buildTtsPage());
    m_stack->addWidget(buildProxyPage());
    m_stack->addWidget(buildPluginsPage());
    m_stack->addWidget(buildAboutPage());

    connect(m_nav, &QListWidget::currentRowChanged,
            m_stack, &QStackedWidget::setCurrentIndex);
    m_nav->setCurrentRow(0);

    rootLayout->addWidget(m_nav);
    rootLayout->addWidget(m_stack, 1);

    // Phase 7: 主题/减少透明度切换时重新应用 backdrop。
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &SettingsWindow::applyBackdrop);
    connect(&ThemeManager::instance(), &ThemeManager::reduceTransparencyChanged,
            this, &SettingsWindow::applyBackdrop);
}

void SettingsWindow::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    // 首次 show 时 HWND 已创建，应用 DWM backdrop。
    applyBackdrop();
}

void SettingsWindow::applyBackdrop()
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
            "[backdrop] SettingsWindow queryBackdrop=%1 (2=Mica,3=Acrylic)")
            .arg(queried);
    }
#else
    // 非 Windows 平台无 DWM backdrop，走假玻璃（qss 已处理）。
#endif
}

// ---------------------------------------------------------------------------
// 常规
// ---------------------------------------------------------------------------
QWidget *SettingsWindow::buildGeneralPage()
{
    ConfigManager &cfg = ConfigManager::instance();
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(14);
    layout->addWidget(makePageTitle(tr("常规"), page));

    auto *form = new QFormLayout;
    form->setSpacing(12);

    // interface language
    auto *langCombo = new QComboBox(page);
    langCombo->addItem(QStringLiteral("简体中文"), QStringLiteral("zh_CN"));
    langCombo->addItem(QStringLiteral("English"), QStringLiteral("en"));
    const int langIdx = langCombo->findData(cfg.uiLanguage());
    langCombo->setCurrentIndex(langIdx >= 0 ? langIdx : 0);
    auto *langHint = makeHint(QString(), page);
    connect(langCombo, &QComboBox::currentIndexChanged, this,
            [langCombo, langHint]() {
        ConfigManager::instance().setValue(QStringLiteral("ui_language"),
                                           langCombo->currentData().toString());
        langHint->setText(tr("语言设置将在重启应用后生效"));
    });
    form->addRow(tr("界面语言"), langCombo);
    form->addRow(QString(), langHint);

    // theme
    auto *themeCombo = new QComboBox(page);
    themeCombo->addItem(tr("浅色"), QStringLiteral("light"));
    themeCombo->addItem(tr("深色"), QStringLiteral("dark"));
    themeCombo->addItem(tr("跟随系统"), QStringLiteral("system"));
    const int themeIdx = themeCombo->findData(cfg.theme());
    themeCombo->setCurrentIndex(themeIdx >= 0 ? themeIdx : 2);
    connect(themeCombo, &QComboBox::currentIndexChanged, this, [themeCombo]() {
        ConfigManager::instance().setValue(QStringLiteral("theme"),
                                           themeCombo->currentData().toString());
    });
    form->addRow(tr("主题"), themeCombo);

    // Phase 7: 减少透明度（可达性开关，放常规页主题组下方）。
    // 即时生效：写 ui.reduce_transparency 配置 → ThemeManager 监听
    // configChanged 信号自动调用 setReduceTransparency → 重新应用 qss
    // + 所有真/假玻璃窗口监听 reduceTransparencyChanged 统一退化。
    auto *reduceTransparencyCheck = new QCheckBox(
        tr("减少透明度（关闭玻璃模糊效果）"), page);
    reduceTransparencyCheck->setChecked(cfg.reduceTransparency());
    reduceTransparencyCheck->setToolTip(tr(
        "开启后所有窗口的玻璃模糊效果退化为不透明卡片，"
        "提升性能与可读性，兼容老系统。即时生效无需重启。"));
    connect(reduceTransparencyCheck, &QCheckBox::toggled, this, [](bool on) {
        ConfigManager::instance().setValue(
            QStringLiteral("ui.reduce_transparency"), on);
    });
    form->addRow(tr("可达性"), reduceTransparencyCheck);

    // Phase 7-fix1：文字小组（译文字号/原文字号/译文颜色/跟随主题）。
    // 即时生效：写 ui.font.* 配置 → MainWindow/PopupCard/OcrResultDialog
    // 监听 configChanged 自动应用。OverlayWindow 不接此配置（按 OCR 框自适应）。
    const int srcPt = cfg.value("ui.font.source_pt").toInt(11);
    const int resPt = cfg.value("ui.font.result_pt").toInt(11);
    const QString resColorMode =
        cfg.value("ui.font.result_color").toString(QStringLiteral("theme"));

    auto *srcPtSpin = new QSpinBox(page);
    srcPtSpin->setRange(9, 28);
    srcPtSpin->setValue(srcPt);
    srcPtSpin->setSuffix(tr(" pt"));
    connect(srcPtSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [](int v) {
                ConfigManager::instance().setValue(
                    QStringLiteral("ui.font.source_pt"), v);
            });
    form->addRow(tr("原文字号"), srcPtSpin);

    auto *resPtSpin = new QSpinBox(page);
    resPtSpin->setRange(9, 28);
    resPtSpin->setValue(resPt);
    resPtSpin->setSuffix(tr(" pt"));
    connect(resPtSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [](int v) {
                ConfigManager::instance().setValue(
                    QStringLiteral("ui.font.result_pt"), v);
            });
    form->addRow(tr("译文字号"), resPtSpin);

    // 译文颜色：色块按钮弹 QColorDialog + 「跟随主题」复选
    auto *colorRow = new QHBoxLayout;
    auto *colorBtn = new QPushButton(page);
    colorBtn->setObjectName(QStringLiteral("iconButton"));
    colorBtn->setFixedSize(40, 28);
    colorBtn->setToolTip(tr("点击选择译文颜色"));
    auto *followThemeCheck = new QCheckBox(tr("跟随主题"), page);
    followThemeCheck->setChecked(resColorMode == QLatin1String("theme"));
    colorRow->addWidget(colorBtn);
    colorRow->addWidget(followThemeCheck);
    colorRow->addStretch();

    auto updateColorBtn = [colorBtn](const QColor &c) {
        // 用 styleSheet 显示当前色块（与 QSS 模板解耦）
        colorBtn->setStyleSheet(
            QStringLiteral("background: %1; border: 1px solid #999;")
                .arg(c.name()));
    };
    const QString customHex =
        cfg.value("ui.font.result_color_custom").toString(QStringLiteral("#1F2937"));
    const QColor initialCustom(customHex);
    updateColorBtn(followThemeCheck->isChecked()
                       ? ThemeManager::instance().palette().textOnGlass
                       : (initialCustom.isValid() ? initialCustom : QColor("#1F2937")));

    connect(colorBtn, &QPushButton::clicked, this,
            [colorBtn, followThemeCheck, updateColorBtn]() {
                // 弹出 QColorDialog，预选当前 custom 色
                auto &cfg = ConfigManager::instance();
                const QString hex = cfg.value("ui.font.result_color_custom")
                                        .toString(QStringLiteral("#1F2937"));
                QColor initial(hex);
                if (!initial.isValid())
                    initial = QColor(QStringLiteral("#1F2937"));
                const QColor chosen = QColorDialog::getColor(
                    initial, nullptr, tr("选择译文颜色"));
                if (chosen.isValid()) {
                    cfg.setValue(QStringLiteral("ui.font.result_color_custom"),
                                 chosen.name());
                    // 选了新色自动切到 custom 模式
                    cfg.setValue(QStringLiteral("ui.font.result_color"),
                                QStringLiteral("custom"));
                    followThemeCheck->setChecked(false);
                    updateColorBtn(chosen);
                }
            });

    connect(followThemeCheck, &QCheckBox::toggled, this,
            [colorBtn, updateColorBtn](bool follow) {
                auto &cfg = ConfigManager::instance();
                if (follow) {
                    cfg.setValue(QStringLiteral("ui.font.result_color"),
                                QStringLiteral("theme"));
                    updateColorBtn(ThemeManager::instance().palette().textOnGlass);
                } else {
                    // 取消跟随：切到 custom，色块显示当前 custom 色
                    cfg.setValue(QStringLiteral("ui.font.result_color"),
                                QStringLiteral("custom"));
                    const QString hex =
                        cfg.value("ui.font.result_color_custom")
                            .toString(QStringLiteral("#1F2937"));
                    QColor c(hex);
                    if (!c.isValid())
                        c = QColor(QStringLiteral("#1F2937"));
                    updateColorBtn(c);
                }
            });

    auto *colorWidget = new QWidget(page);
    colorWidget->setLayout(colorRow);
    form->addRow(tr("译文颜色"), colorWidget);

    auto *colorHint = makeHint(
        tr("自定义颜色请自行确认深色模式可读性"), page);
    form->addRow(QString(), colorHint);

    // autostart (phase 5: 联动 AutoStartManager 同步注册表)
    auto *autostartCheck = new QCheckBox(tr("开机启动（驻留托盘）"), page);
    autostartCheck->setChecked(cfg.autostart());
    connect(autostartCheck, &QCheckBox::toggled, this, [autostartCheck](bool on) {
        // 写注册表失败时回滚 checkbox 状态并提示用户。
        if (!AutoStartManager::instance().setEnabled(on)) {
            autostartCheck->blockSignals(true);
            autostartCheck->setChecked(!on);
            autostartCheck->blockSignals(false);
            return;
        }
        ConfigManager::instance().setValue(QStringLiteral("autostart"), on);
    });
    form->addRow(tr("启动"), autostartCheck);

    // selection translate switch
    auto *selectionCheck = new QCheckBox(tr("启用划词翻译"), page);
    selectionCheck->setChecked(cfg.selectionEnabled());
    connect(selectionCheck, &QCheckBox::toggled, this, [](bool on) {
        ConfigManager::instance().setValue(QStringLiteral("selection.enabled"), on);
    });
    form->addRow(tr("划词"), selectionCheck);

    // history limit
    auto *limitSpin = new QSpinBox(page);
    limitSpin->setRange(100, 100000);
    limitSpin->setSingleStep(100);
    limitSpin->setValue(cfg.historyLimit());
    connect(limitSpin, &QSpinBox::editingFinished, this, [limitSpin]() {
        ConfigManager::instance().setValue(QStringLiteral("history.limit"),
                                           limitSpin->value());
    });
    form->addRow(tr("历史记录上限"), limitSpin);

    layout->addLayout(form);

    // ---- 通知开关分组（phase 5）------------------------------------------------
    layout->addSpacing(6);
    layout->addWidget(makePageTitle(tr("通知"), page));
    layout->addWidget(makeHint(
        tr("总开关关闭时所有气泡通知都不显示；总开关开启时下方场景可单独控制。"),
        page));

    auto *notifForm = new QFormLayout;
    notifForm->setSpacing(10);

    m_notifMaster = new QCheckBox(tr("启用气泡通知"), page);
    m_notifMaster->setChecked(cfg.notificationsEnabled());
    connect(m_notifMaster, &QCheckBox::toggled, this, [this](bool on) {
        ConfigManager::instance().setValue(QStringLiteral("notifications.enabled"), on);
        // 总开关变化时刷新逐项启用状态。
        for (auto it = m_notifCategoryChecks.begin();
             it != m_notifCategoryChecks.end(); ++it)
            it.value()->setEnabled(on);
    });
    notifForm->addRow(tr("总开关"), m_notifMaster);

    // 场景类目：capture_ocr / capture_translate / selection / replace / translate_failed。
    // hotkey_conflict（v0.7.2）：默认关，开启后启动时热键被占才弹气泡
    // （关闭时仅写日志）。
    struct NotifCat { const char *key; const char *label; };
    const NotifCat cats[] = {
        {"capture_ocr",       QT_TRANSLATE_NOOP("SettingsWindow", "截图 OCR 完成")},
        {"capture_translate", QT_TRANSLATE_NOOP("SettingsWindow", "截图翻译完成")},
        {"selection",         QT_TRANSLATE_NOOP("SettingsWindow", "划词翻译提示")},
        {"replace",           QT_TRANSLATE_NOOP("SettingsWindow", "文本替换结果")},
        {"translate_failed",  QT_TRANSLATE_NOOP("SettingsWindow", "翻译服务不可达提示")},
        {"hotkey_conflict",   QT_TRANSLATE_NOOP("SettingsWindow", "热键冲突提示（默认关，仅写日志）")},
    };
    for (const NotifCat &c : cats) {
        auto *cb = new QCheckBox(tr(c.label), page);
        cb->setChecked(cfg.notificationCategoryEnabled(QString::fromLatin1(c.key)));
        cb->setEnabled(cfg.notificationsEnabled());
        const QString key = QString::fromLatin1(c.key);
        connect(cb, &QCheckBox::toggled, this, [key](bool on) {
            ConfigManager::instance().setValue(
                QStringLiteral("notifications.") + key, on);
        });
        m_notifCategoryChecks.insert(key, cb);
        notifForm->addRow(QString(), cb);
    }
    layout->addLayout(notifForm);

    layout->addStretch(1);
    return page;
}

// ---------------------------------------------------------------------------
// 快捷键
// ---------------------------------------------------------------------------
QWidget *SettingsWindow::buildHotkeyPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(10);
    layout->addWidget(makePageTitle(tr("快捷键"), page));
    layout->addWidget(makeHint(
        tr("点击输入框后按下新组合键，再点“应用”立即生效并保存。"), page));

    ConfigManager &cfg = ConfigManager::instance();
    const QVector<HotkeyManager::ActionSpec> specs = HotkeyManager::defaultActions();
    for (const HotkeyManager::ActionSpec &spec : specs) {
        auto *row = new QHBoxLayout;
        row->setSpacing(8);

        auto *label = new QLabel(spec.label, page);
        label->setFixedWidth(120);

        auto *edit = new QKeySequenceEdit(page);
        edit->setMaximumSequenceLength(1);
        const QString saved = cfg.hotkeyFor(spec.actionId);
        edit->setKeySequence(QKeySequence(
            saved.isEmpty() ? spec.sequence.toString(QKeySequence::PortableText)
                            : saved));

        auto *applyBtn = new QPushButton(tr("应用"), page);
        auto *status = new QLabel(page);
        status->setObjectName(QStringLiteral("hintLabel"));

        const QString actionId = spec.actionId;
        connect(applyBtn, &QPushButton::clicked, this,
                [this, actionId]() { applyHotkey(actionId); });

        row->addWidget(label);
        row->addWidget(edit, 1);
        row->addWidget(applyBtn);
        row->addWidget(status, 1);
        layout->addLayout(row);

        m_hotkeyRows.insert(actionId, HotkeyRow{edit, status});
    }

    auto *resetBtn = new QPushButton(tr("恢复默认"), page);
    resetBtn->setObjectName(QStringLiteral("ghostButton"));
    connect(resetBtn, &QPushButton::clicked,
            this, &SettingsWindow::restoreDefaultHotkeys);
    auto *bottomRow = new QHBoxLayout;
    bottomRow->addWidget(resetBtn);
    bottomRow->addStretch(1);
    layout->addSpacing(6);
    layout->addLayout(bottomRow);
    layout->addStretch(1);
    return page;
}

void SettingsWindow::applyHotkey(const QString &actionId)
{
    const HotkeyRow row = m_hotkeyRows.value(actionId);
    if (!row.edit || !row.status)
        return;

    const QKeySequence seq = row.edit->keySequence();
    if (seq.isEmpty()) {
        row.status->setObjectName(QStringLiteral("errorLabel"));
        row.status->setText(tr("快捷键不能为空"));
        row.status->style()->unpolish(row.status);
        row.status->style()->polish(row.status);
        return;
    }

    ConfigManager &cfg = ConfigManager::instance();
    const QString previous = cfg.hotkeyFor(actionId);

    HotkeyManager &hotkeys = HotkeyManager::instance();
    if (hotkeys.rebind(actionId, seq)) {
        cfg.setHotkey(actionId, seq.toString(QKeySequence::PortableText));
        row.status->setObjectName(QStringLiteral("okLabel"));
        row.status->setText(tr("已生效"));
    } else {
        // Roll the registration back to the previous binding.
        hotkeys.rebind(actionId, QKeySequence(previous));
        row.status->setObjectName(QStringLiteral("errorLabel"));
        row.status->setText(tr("注册失败：该组合键可能已被其他程序占用"));
    }
    row.status->style()->unpolish(row.status);
    row.status->style()->polish(row.status);
}

void SettingsWindow::restoreDefaultHotkeys()
{
    const QVector<HotkeyManager::ActionSpec> specs = HotkeyManager::defaultActions();
    for (const HotkeyManager::ActionSpec &spec : specs) {
        const HotkeyRow row = m_hotkeyRows.value(spec.actionId);
        if (row.edit)
            row.edit->setKeySequence(spec.sequence);
        applyHotkey(spec.actionId);
    }
}

// ---------------------------------------------------------------------------
// 翻译服务
// ---------------------------------------------------------------------------
QString SettingsWindow::providerDisplayName(const QString &name)
{
    if (name == QLatin1String("google")) return tr("Google（免费）");
    if (name == QLatin1String("mymemory")) return tr("MyMemory（免费）");
    if (name == QLatin1String("bing"))   return tr("Bing（免费）");
    if (name == QLatin1String("deepl"))  return QStringLiteral("DeepL");
    if (name == QLatin1String("baidu"))  return tr("百度翻译");
    if (name == QLatin1String("youdao")) return tr("有道智云");
    if (name == QLatin1String("tencent")) return tr("腾讯云 TMT");
    if (name == QLatin1String("openai")) return tr("OpenAI 兼容");
    if (name == QLatin1String("zhipu")) return tr("智谱 GLM（免费）");
    if (name == QLatin1String("deeplx")) return QStringLiteral("DeepLX");
    return name;
}

QWidget *SettingsWindow::buildProvidersPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(10);
    layout->addWidget(makePageTitle(tr("翻译服务"), page));
    layout->addWidget(makeHint(
        tr("勾选启用服务；顺序即调度优先级。未配置密钥的服务显示为灰色，"
           "不参与自动调度。"), page));

    auto *body = new QHBoxLayout;
    // v0.7.1 UI：左列表与右侧配置区拉开间距，避免拥挤。
    body->setSpacing(22);

    // left: priority list + reorder buttons
    auto *leftCol = new QVBoxLayout;
    m_providerList = new QListWidget(page);
    // v0.7.1 UI：独立 objectName 走 QSS 专属规则（项高≥34px、项间留白、
    // 选中态半透明灰底+左侧主色竖条，hover 弱反馈）。
    m_providerList->setObjectName(QStringLiteral("providerList"));
    m_providerList->setSelectionMode(QAbstractItemView::SingleSelection);
    // 项间留白：QListView::setSpacing 对每项四周加留白（QSS margin 对
    // ::item 不可靠，用视图层 API 保证三态主题一致）。
    m_providerList->setSpacing(2);
    leftCol->addWidget(m_providerList, 1);

    auto *orderRow = new QHBoxLayout;
    auto *upBtn = new QPushButton(tr("上移"), page);
    auto *downBtn = new QPushButton(tr("下移"), page);
    upBtn->setObjectName(QStringLiteral("ghostButton"));
    downBtn->setObjectName(QStringLiteral("ghostButton"));
    orderRow->addWidget(upBtn);
    orderRow->addWidget(downBtn);
    orderRow->addStretch(1);
    leftCol->addLayout(orderRow);
    body->addLayout(leftCol, 4);

    // right: per-provider config form + test
    auto *rightCol = new QVBoxLayout;
    m_providerFormHost = new QWidget(page);
    m_providerForm = new QFormLayout(m_providerFormHost);
    m_providerForm->setSpacing(10);
    rightCol->addWidget(m_providerFormHost);

    auto *testRow = new QHBoxLayout;
    m_providerTestButton = new QPushButton(tr("测试"), page);
    m_providerTestResult = new QLabel(page);
    m_providerTestResult->setObjectName(QStringLiteral("hintLabel"));
    m_providerTestResult->setWordWrap(true);
    testRow->addWidget(m_providerTestButton);
    testRow->addWidget(m_providerTestResult, 1);
    rightCol->addLayout(testRow);
    rightCol->addStretch(1);
    body->addLayout(rightCol, 6);

    layout->addLayout(body, 1);

    connect(upBtn, &QPushButton::clicked, this, [this]() { moveProvider(-1); });
    connect(downBtn, &QPushButton::clicked, this, [this]() { moveProvider(1); });
    connect(m_providerList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *current, QListWidgetItem *previous) {
        // v0.7.1 UI：选中项文字加粗（QSS ::item 不支持 font-weight，
        // 用 item 字体实现，三态主题通用）。
        if (previous) {
            QFont f = previous->font();
            f.setBold(false);
            previous->setFont(f);
        }
        if (current) {
            QFont f = current->font();
            f.setBold(true);
            current->setFont(f);
            rebuildProviderForm(current->data(Qt::UserRole).toString());
        }
    });
    connect(m_providerList, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *item) {
        if (m_providerListLoading || !item)
            return;
        const QString name = item->data(Qt::UserRole).toString();
        ConfigManager::instance().setProviderField(
            name, QStringLiteral("enabled"), item->checkState() == Qt::Checked);
    });
    connect(m_providerTestButton, &QPushButton::clicked, this, [this]() {
        QListWidgetItem *item = m_providerList->currentItem();
        if (item)
            testProvider(item->data(Qt::UserRole).toString());
    });

    reloadProviderList();
    return page;
}

void SettingsWindow::reloadProviderList(const QString &selectName)
{
    m_providerListLoading = true;
    m_providerList->clear();

    ConfigManager &cfg = ConfigManager::instance();
    TranslationManager &mgr = TranslationManager::instance();
    int selectRow = 0;
    const QStringList order = cfg.providerOrder();
    for (const QString &name : order) {
        Translator *t = mgr.providerByName(name);
        if (!t)
            continue;
        auto *item = new QListWidgetItem(providerDisplayName(name));
        item->setData(Qt::UserRole, name);
        // v0.7.1 UI：项高加大到 36px（QSS ::item min-height 不能驱动
        // delegate sizeHint，真机实测仍拥挤，改用 setSizeHint 硬保证）。
        item->setSizeHint(QSize(0, 36));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        const QJsonObject pc = cfg.providerConfig(name);
        item->setCheckState(pc.value(QStringLiteral("enabled")).toBool()
                                ? Qt::Checked : Qt::Unchecked);
        if (!t->isConfigured()) {
            // Visible but greyed out: needs credentials before scheduling.
            // v0.7.1 UI：灰色跟随主题 subText（浅/深主题下与启用项亮度
            // 差异都清晰），不再用固定色。
            item->setForeground(ThemeManager::instance().palette().subText);
            item->setToolTip(tr("未配置密钥，暂不参与自动调度"));
        }
        m_providerList->addItem(item);
        if (name == selectName)
            selectRow = m_providerList->count() - 1;
    }
    m_providerListLoading = false;
    if (m_providerList->count() > 0)
        m_providerList->setCurrentRow(selectRow);
}

void SettingsWindow::rebuildProviderForm(const QString &providerName)
{
    // Clear the previous form.
    while (m_providerForm->rowCount() > 0)
        m_providerForm->removeRow(0);
    m_providerTestResult->clear();

    const QList<FieldSpec> fields = fieldsFor(providerName);
    if (fields.isEmpty()) {
        m_providerForm->addRow(
            makeHint(tr("该服务免密钥，无需配置。"), m_providerFormHost));
        return;
    }

    ConfigManager &cfg = ConfigManager::instance();
    const QJsonObject pc = cfg.providerConfig(providerName);
    for (const FieldSpec &spec : fields) {
        auto *edit = new QLineEdit(m_providerFormHost);
        edit->setText(pc.value(QLatin1String(spec.key)).toString());
        if (spec.secret)
            edit->setEchoMode(QLineEdit::Password);
        const QString fieldKey = QLatin1String(spec.key);
        connect(edit, &QLineEdit::editingFinished, this,
                [this, providerName, fieldKey, edit]() {
            ConfigManager::instance().setProviderField(
                providerName, fieldKey, edit->text().trimmed());
            // Credentials may flip isConfigured -> refresh grey state.
            reloadProviderList(providerName);
        });
        m_providerForm->addRow(tr(spec.label), edit);
    }
}

void SettingsWindow::moveProvider(int delta)
{
    const int row = m_providerList->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= m_providerList->count())
        return;

    ConfigManager &cfg = ConfigManager::instance();
    QStringList order = cfg.providerOrder();
    if (row >= order.size() || target >= order.size())
        return;
    order.swapItemsAt(row, target);
    cfg.setProviderOrder(order);
    reloadProviderList(order.at(target));
}

void SettingsWindow::testProvider(const QString &providerName)
{
    Translator *t = TranslationManager::instance().providerByName(providerName);
    if (!t)
        return;
    m_providerTestButton->setEnabled(false);
    m_providerTestResult->setText(tr("测试中…"));

    // Phase 7-fix2 BUG5：稳定性修复——provider 测试卡死/崩溃退出根因：
    //   1) watcher 是 this 子对象，SettingsWindow 测试中销毁 → finished lambda
    //      捕获 this/use-after-free；2) watcher 永不 finished（异常逃出 lambda 或
    //      QNetworkReply 挂起）→ 按钮永久 disabled；3) provider 内部 JSON 解析等
    //      抛异常时无 try/catch → 进程崩溃退出。
    //   修复：QPointer 守卫防 use-after-free + try/catch 兜底防崩 + 10s 兜底超时
    //   防按钮永久 disabled。provider 层已有 3s 传输超时（kTimeoutMs），这里再加
    //   一道 UI 层兜底，互不冲突。
    QPointer<SettingsWindow> guard(this);
    QPointer<QPushButton> btnGuard(m_providerTestButton);
    QPointer<QLabel> labelGuard(m_providerTestResult);

    auto *watcher = new QFutureWatcher<TransResult>(this);
    QPointer<QFutureWatcher<TransResult>> watcherGuard(watcher);

    connect(watcher, &QFutureWatcher<TransResult>::finished, this,
            [guard, btnGuard, labelGuard, watcherGuard]() {
        // 兜底复位按钮（无论窗口是否仍存活，避免按钮永久 disabled）
        if (btnGuard)
            btnGuard->setEnabled(true);
        if (watcherGuard)
            watcherGuard->deleteLater();
        if (!guard || !labelGuard)
            return;  // 窗口已销毁，不再访问 UI

        try {
            const TransResult r = watcherGuard->result();
            if (r.error.isEmpty()) {
                labelGuard->setObjectName(QStringLiteral("okLabel"));
                labelGuard->setText(
                    tr("hello → %1（%2 ms）").arg(r.text).arg(r.elapsedMs));
            } else {
                labelGuard->setObjectName(QStringLiteral("errorLabel"));
                labelGuard->setText(r.error);
            }
            labelGuard->style()->unpolish(labelGuard);
            labelGuard->style()->polish(labelGuard);
        } catch (...) {
            // Phase 7-fix2 BUG5：异常兜底防进程崩溃退出
            labelGuard->setObjectName(QStringLiteral("errorLabel"));
            labelGuard->setText(tr("测试异常：内部错误"));
            labelGuard->style()->unpolish(labelGuard);
            labelGuard->style()->polish(labelGuard);
        }
    });

    // 兜底超时：provider 层 3s 超时失效时（如 QNetworkReply 卡在底层 SSL 握手），
    // 10s 后强制复位按钮状态，防止"测试中…"永久停留
    QTimer::singleShot(10000, this, [guard, btnGuard, labelGuard, watcherGuard]() {
        if (!watcherGuard)
            return;  // watcher 已正常完成并 deleteLater
        if (btnGuard)
            btnGuard->setEnabled(true);
        if (labelGuard) {
            labelGuard->setObjectName(QStringLiteral("errorLabel"));
            labelGuard->setText(tr("测试超时"));
            labelGuard->style()->unpolish(labelGuard);
            labelGuard->style()->polish(labelGuard);
        }
        if (watcherGuard) {
            watcherGuard->cancel();
            watcherGuard->deleteLater();
        }
    });

    try {
        watcher->setFuture(t->translate(QStringLiteral("hello"),
                                        QStringLiteral("en"),
                                        QStringLiteral("zh-CN")));
    } catch (...) {
        // Phase 7-fix2 BUG5：translate 入口异常兜底（理论不应发生，但 provider
        // 实现可能抛 std::bad_alloc 等）
        m_providerTestButton->setEnabled(true);
        m_providerTestResult->setObjectName(QStringLiteral("errorLabel"));
        m_providerTestResult->setText(tr("测试启动失败：内部错误"));
        m_providerTestResult->style()->unpolish(m_providerTestResult);
        m_providerTestResult->style()->polish(m_providerTestResult);
        watcher->deleteLater();
    }
}

// ---------------------------------------------------------------------------
// OCR
// ---------------------------------------------------------------------------
QWidget *SettingsWindow::buildOcrPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(14);
    layout->addWidget(makePageTitle(tr("OCR"), page));

    auto *paddleRadio = new QRadioButton(tr("本地 Paddle OCR（内置模型）"), page);
    auto *systemRadio = new QRadioButton(tr("系统 OCR（Windows.Media.Ocr）"), page);

    // 系统 OCR 仅在 WinRT 头可用 + 系统存在 OCR 语言包时可选。
    const bool systemOk = SystemOcrEngine::isAvailable();
    systemRadio->setEnabled(systemOk);
    if (!systemOk) {
        systemRadio->setToolTip(tr("本机无 C++/WinRT 头或未安装 OCR 语言包，"
                                   "请在 Windows 设置 → 时间和语言 → 语言 中添加 OCR 语言包"));
    }

    const QString engine = ConfigManager::instance().ocrEngine();
    if (engine == QLatin1String("system") && systemOk) {
        systemRadio->setChecked(true);
    } else {
        paddleRadio->setChecked(true);
    }
    connect(paddleRadio, &QRadioButton::toggled, this, [](bool on) {
        if (on)
            ConfigManager::instance().setValue(QStringLiteral("ocr.engine"),
                                               QStringLiteral("paddle"));
    });
    connect(systemRadio, &QRadioButton::toggled, this, [systemRadio](bool on) {
        if (on && systemRadio->isEnabled())
            ConfigManager::instance().setValue(QStringLiteral("ocr.engine"),
                                               QStringLiteral("system"));
    });

    layout->addWidget(paddleRadio);
    layout->addWidget(systemRadio);
    layout->addWidget(makeHint(
        tr("系统 OCR 不依赖 ONNX 模型，启动更快；Paddle OCR 精度更高且支持坐标输出。"
           "切换立即生效，截图 OCR / 截图翻译 / 区域 OCR 共用此设置。"),
        page));
    layout->addStretch(1);
    return page;
}

// ---------------------------------------------------------------------------
// 朗读 (TTS)
// ---------------------------------------------------------------------------
QWidget *SettingsWindow::buildTtsPage()
{
    ConfigManager &cfg = ConfigManager::instance();
    TtsManager &tts = TtsManager::instance();

    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(14);
    layout->addWidget(makePageTitle(tr("朗读"), page));

    auto *enableCheck = new QCheckBox(tr("启用朗读（TTS）"), page);
    enableCheck->setChecked(cfg.ttsEnabled());
    connect(enableCheck, &QCheckBox::toggled, this, [](bool on) {
        ConfigManager::instance().setValue(QStringLiteral("tts.enabled"), on);
    });
    layout->addWidget(enableCheck);

    auto *form = new QFormLayout;
    form->setSpacing(12);

    // v0.7.2：朗读引擎选择。cloud=Edge 免费神经嗓音（默认，全语种、无需
    // 本机语音包），system=本机 QTextToSpeech。切换立即写配置并刷新
    // 嗓音下拉数据源（云端列 Edge voices，系统列本机 voices）。
    auto *engineCombo = new QComboBox(page);
    engineCombo->setObjectName(QStringLiteral("ttsEngineCombo"));
    engineCombo->addItem(tr("云端（Edge，推荐）"), QStringLiteral("cloud"));
    engineCombo->addItem(tr("系统嗓音"), QStringLiteral("system"));
    engineCombo->setCurrentIndex(
        TtsManager::engine() == QLatin1String("system") ? 1 : 0);

    // v0.7.1 BUG-A：不再写死中/英两个嗓音下拉，改为"语言 + 嗓音"两级
    // 选择；配置键按语言族动态：系统引擎 tts.voice.<lang>（TtsManager::
    // voiceConfigKey），云端引擎 tts.cloud_voice.<lang>（EdgeTtsProvider::
    // voiceConfigKey），两套偏好互不干扰。
    auto *langCombo = new QComboBox(page);
    // objectName 供 --shot 证据场景定位（新增名，红线允许）。
    langCombo->setObjectName(QStringLiteral("ttsLangCombo"));
    {
        int n = 0;
        const LangEntry *langs = langCatalog(&n);
        for (int i = 0; i < n; ++i) {
            if (qstrcmp(langs[i].code, "auto") == 0)
                continue;
            langCombo->addItem(
                QCoreApplication::translate("MainWindow", langs[i].label),
                QString::fromLatin1(langs[i].code));
        }
    }
    auto *voiceCombo = new QComboBox(page);
    voiceCombo->setObjectName(QStringLiteral("ttsVoiceCombo"));
    // 回退提示沿用 hintLabel 样式（objectName 不改，QSS 选择器依赖）。
    auto *fallbackHint = makeHint(QString(), page);

    // 引擎/语言切换 → 重建嗓音列表 + 预选当前配置（blockSignals 防重建
    // 过程误写）。云端数据源是内置 Edge 映射表，不依赖本机语音包。
    const auto reloadVoices = [voiceCombo, langCombo, engineCombo,
                               fallbackHint]() {
        const QString code = langCombo->currentData().toString();
        const bool cloud = engineCombo->currentData().toString()
                           == QLatin1String("cloud");
        ConfigManager &cfg = ConfigManager::instance();

        voiceCombo->blockSignals(true);
        voiceCombo->clear();
        voiceCombo->addItem(tr("自动选择"), QString());
        QStringList names;
        QString saved;
        if (cloud) {
            names = EdgeTtsProvider::voicesForLang(code);
            saved = cfg.stringValue(EdgeTtsProvider::voiceConfigKey(code));
        } else {
            names = TtsManager::instance().voiceNamesFor(code);
            saved = cfg.stringValue(TtsManager::voiceConfigKey(code));
            if (saved.isEmpty()) {
                // legacy 回退：v0.7.0 前只有 voice_zh/voice_en 两个固定键。
                if (code.startsWith(QLatin1String("zh")))
                    saved = cfg.stringValue(QStringLiteral("tts.voice_zh"));
                else if (code == QLatin1String("en"))
                    saved = cfg.stringValue(QStringLiteral("tts.voice_en"));
            }
        }
        for (const QString &name : names)
            voiceCombo->addItem(name, name);
        const int idx = voiceCombo->findData(saved);
        voiceCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        voiceCombo->blockSignals(false);

        // 无可选嗓音时明示回退去向，不静默。
        if (cloud) {
            fallbackHint->setText(names.isEmpty()
                ? tr("该语言暂无内置 Edge 嗓音映射，将使用多语言默认嗓音")
                : QString());
        } else {
            fallbackHint->setText(names.isEmpty()
                ? tr("当前语言无专用嗓音，朗读时将使用系统默认嗓音")
                : QString());
        }
        fallbackHint->setVisible(names.isEmpty());
    };
    connect(engineCombo, &QComboBox::currentIndexChanged, this,
            [engineCombo, reloadVoices]() {
        ConfigManager::instance().setValue(
            QStringLiteral("tts.engine"),
            engineCombo->currentData().toString());
        reloadVoices();
    });
    connect(langCombo, &QComboBox::currentIndexChanged, this, reloadVoices);
    connect(voiceCombo, &QComboBox::currentIndexChanged, this,
            [voiceCombo, langCombo, engineCombo]() {
        const QString code = langCombo->currentData().toString();
        const QString name = voiceCombo->currentData().toString();
        ConfigManager &cfg = ConfigManager::instance();
        if (engineCombo->currentData().toString()
                == QLatin1String("cloud")) {
            cfg.setValue(EdgeTtsProvider::voiceConfigKey(code), name);
            return;
        }
        cfg.setValue(TtsManager::voiceConfigKey(code), name);
        // 镜像写 legacy 键，保持旧回退链与新键一致（避免"清空新键后
        // 又被残留 legacy 值顶回去"的不一致）。
        if (code.startsWith(QLatin1String("zh")))
            cfg.setValue(QStringLiteral("tts.voice_zh"), name);
        else if (code == QLatin1String("en"))
            cfg.setValue(QStringLiteral("tts.voice_en"), name);
    });
    reloadVoices();

    form->addRow(tr("朗读引擎"), engineCombo);
    form->addRow(tr("语言"), langCombo);
    form->addRow(tr("偏好嗓音"), voiceCombo);
    form->addRow(QString(), fallbackHint);
    layout->addLayout(form);

    // v0.7.2 合规灰字：云端接口性质与回退语义明示。
    layout->addWidget(makeHint(
        tr("云端语音为非官方免费接口，需联网，仅供个人学习；"
           "失败时自动回退系统嗓音。"),
        page));

    auto *testBtn = new QPushButton(tr("测试朗读"), page);
    connect(testBtn, &QPushButton::clicked, this, [langCombo]() {
        // 用当前选中语言朗读测试句；speak() 按配置引擎分派（引擎下拉
        // 切换即写配置），直接验证云端/系统链路与回退。
        TtsManager::instance().speak(
            tr("你好，这是 X翻译 的朗读测试。Hello from XTranslate."),
            langCombo->currentData().toString());
    });
    auto *testRow = new QHBoxLayout;
    testRow->addWidget(testBtn);
    testRow->addStretch(1);
    layout->addLayout(testRow);

    // v0.7.2：本机无系统语音引擎时不再整页禁用（云端引擎不依赖本机
    // 语音包），仅提示回退能力受限。
    if (!tts.isAvailable()) {
        layout->addWidget(makeHint(
            tr("本机没有可用的系统语音引擎；云端（Edge）引擎仍可用，"
               "但失败时无法回退本机嗓音。"),
            page));
    }
    layout->addStretch(1);
    return page;
}

// ---------------------------------------------------------------------------
// 代理
// ---------------------------------------------------------------------------
QWidget *SettingsWindow::buildProxyPage()
{
    ConfigManager &cfg = ConfigManager::instance();
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);
    layout->addWidget(makePageTitle(tr("代理"), page));

    m_proxyNone = new QRadioButton(tr("不使用代理"), page);
    m_proxySystem = new QRadioButton(tr("使用系统代理"), page);
    m_proxyManual = new QRadioButton(tr("手动配置（HTTP 代理）"), page);
    layout->addWidget(m_proxyNone);
    layout->addWidget(m_proxySystem);
    layout->addWidget(m_proxyManual);

    auto *form = new QFormLayout;
    form->setSpacing(10);
    m_proxyHost = new QLineEdit(page);
    m_proxyPort = new QSpinBox(page);
    m_proxyPort->setRange(0, 65535);
    m_proxyUser = new QLineEdit(page);
    m_proxyPass = new QLineEdit(page);
    m_proxyPass->setEchoMode(QLineEdit::Password);
    form->addRow(tr("主机"), m_proxyHost);
    form->addRow(tr("端口"), m_proxyPort);
    form->addRow(tr("用户名（可选）"), m_proxyUser);
    form->addRow(tr("密码（可选）"), m_proxyPass);
    layout->addLayout(form);

    // load current values
    const QString mode = cfg.stringValue(QStringLiteral("proxy.mode"));
    if (mode == QLatin1String("system"))
        m_proxySystem->setChecked(true);
    else if (mode == QLatin1String("manual"))
        m_proxyManual->setChecked(true);
    else
        m_proxyNone->setChecked(true);
    m_proxyHost->setText(cfg.stringValue(QStringLiteral("proxy.host")));
    m_proxyPort->setValue(cfg.intValue(QStringLiteral("proxy.port")));
    m_proxyUser->setText(cfg.stringValue(QStringLiteral("proxy.user")));
    m_proxyPass->setText(cfg.stringValue(QStringLiteral("proxy.pass")));

    const auto updateEnabled = [this]() {
        const bool manual = m_proxyManual->isChecked();
        m_proxyHost->setEnabled(manual);
        m_proxyPort->setEnabled(manual);
        m_proxyUser->setEnabled(manual);
        m_proxyPass->setEnabled(manual);
    };
    updateEnabled();

    const auto saveMode = [this, updateEnabled]() {
        QString newMode = QStringLiteral("none");
        if (m_proxySystem->isChecked())
            newMode = QStringLiteral("system");
        else if (m_proxyManual->isChecked())
            newMode = QStringLiteral("manual");
        ConfigManager &cfg = ConfigManager::instance();
        cfg.setValue(QStringLiteral("proxy.mode"), newMode);
        // 用户显式选过代理模式，标记后迁移逻辑不再覆盖用户选择。
        cfg.setValue(QStringLiteral("proxy.user_touched"), true);
        updateEnabled();
    };
    connect(m_proxyNone, &QRadioButton::toggled, this, [saveMode](bool on) {
        if (on) saveMode();
    });
    connect(m_proxySystem, &QRadioButton::toggled, this, [saveMode](bool on) {
        if (on) saveMode();
    });
    connect(m_proxyManual, &QRadioButton::toggled, this, [saveMode](bool on) {
        if (on) saveMode();
    });
    connect(m_proxyHost, &QLineEdit::editingFinished, this, [this]() {
        ConfigManager &cfg = ConfigManager::instance();
        cfg.setValue(QStringLiteral("proxy.host"), m_proxyHost->text().trimmed());
        cfg.setValue(QStringLiteral("proxy.user_touched"), true);
    });
    connect(m_proxyPort, &QSpinBox::editingFinished, this, [this]() {
        ConfigManager &cfg = ConfigManager::instance();
        cfg.setValue(QStringLiteral("proxy.port"), m_proxyPort->value());
        cfg.setValue(QStringLiteral("proxy.user_touched"), true);
    });
    connect(m_proxyUser, &QLineEdit::editingFinished, this, [this]() {
        ConfigManager &cfg = ConfigManager::instance();
        cfg.setValue(QStringLiteral("proxy.user"), m_proxyUser->text());
        cfg.setValue(QStringLiteral("proxy.user_touched"), true);
    });
    connect(m_proxyPass, &QLineEdit::editingFinished, this, [this]() {
        ConfigManager &cfg = ConfigManager::instance();
        cfg.setValue(QStringLiteral("proxy.pass"), m_proxyPass->text());
        cfg.setValue(QStringLiteral("proxy.user_touched"), true);
    });

    // test: one google translation through the currently applied proxy
    auto *testBtn = new QPushButton(tr("测试"), page);
    m_proxyTestResult = new QLabel(page);
    m_proxyTestResult->setObjectName(QStringLiteral("hintLabel"));
    m_proxyTestResult->setWordWrap(true);
    connect(testBtn, &QPushButton::clicked, this, [this, testBtn]() {
        testBtn->setEnabled(false);
        m_proxyTestResult->setText(tr("测试中…"));
        Translator *g =
            TranslationManager::instance().providerByName(QStringLiteral("google"));
        // Phase 7-fix2 BUG5：代理页同步加固（与 testProvider 同款 QPointer+try/catch+10s 兜底）
        QPointer<SettingsWindow> guard(this);
        QPointer<QPushButton> btnGuard(testBtn);
        QPointer<QLabel> labelGuard(m_proxyTestResult);

        auto *watcher = new QFutureWatcher<TransResult>(this);
        QPointer<QFutureWatcher<TransResult>> watcherGuard(watcher);
        connect(watcher, &QFutureWatcher<TransResult>::finished, this,
                [guard, btnGuard, labelGuard, watcherGuard]() {
            if (btnGuard)
                btnGuard->setEnabled(true);
            if (watcherGuard)
                watcherGuard->deleteLater();
            if (!guard || !labelGuard)
                return;

            try {
                const TransResult r = watcherGuard->result();
                if (r.error.isEmpty()) {
                    labelGuard->setObjectName(QStringLiteral("okLabel"));
                    labelGuard->setText(
                        tr("代理连通：hello → %1（%2 ms）").arg(r.text).arg(r.elapsedMs));
                } else {
                    labelGuard->setObjectName(QStringLiteral("errorLabel"));
                    labelGuard->setText(tr("代理测试失败：%1").arg(r.error));
                }
                labelGuard->style()->unpolish(labelGuard);
                labelGuard->style()->polish(labelGuard);
            } catch (...) {
                labelGuard->setObjectName(QStringLiteral("errorLabel"));
                labelGuard->setText(tr("代理测试异常：内部错误"));
                labelGuard->style()->unpolish(labelGuard);
                labelGuard->style()->polish(labelGuard);
            }
        });

        QTimer::singleShot(10000, this, [guard, btnGuard, labelGuard, watcherGuard]() {
            if (!watcherGuard)
                return;
            if (btnGuard)
                btnGuard->setEnabled(true);
            if (labelGuard) {
                labelGuard->setObjectName(QStringLiteral("errorLabel"));
                labelGuard->setText(tr("代理测试超时"));
                labelGuard->style()->unpolish(labelGuard);
                labelGuard->style()->polish(labelGuard);
            }
            if (watcherGuard) {
                watcherGuard->cancel();
                watcherGuard->deleteLater();
            }
        });

        try {
            watcher->setFuture(g->translate(QStringLiteral("hello"),
                                            QStringLiteral("en"),
                                            QStringLiteral("zh-CN")));
        } catch (...) {
            testBtn->setEnabled(true);
            m_proxyTestResult->setObjectName(QStringLiteral("errorLabel"));
            m_proxyTestResult->setText(tr("代理测试启动失败：内部错误"));
            m_proxyTestResult->style()->unpolish(m_proxyTestResult);
            m_proxyTestResult->style()->polish(m_proxyTestResult);
            watcher->deleteLater();
        }
    });
    auto *testRow = new QHBoxLayout;
    testRow->addWidget(testBtn);
    testRow->addWidget(m_proxyTestResult, 1);
    layout->addLayout(testRow);
    layout->addStretch(1);
    return page;
}

// ---------------------------------------------------------------------------
// 插件 (phase 5)
// ---------------------------------------------------------------------------
QWidget *SettingsWindow::buildPluginsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(10);
    layout->addWidget(makePageTitle(tr("插件"), page));
    layout->addWidget(makeHint(
        tr("插件目录下的每个子目录视为一个进程外翻译插件（plugin.exe|plugin.py|plugin.bat）。"
           "环境变量 XTRANSLATE_PLUGINS_DIR 完全取代默认路径 %APPDATA%\\XTranslate\\plugins\\。"),
        page));

    m_pluginDirLabel = new QLabel(page);
    m_pluginDirLabel->setObjectName(QStringLiteral("hintLabel"));
    m_pluginDirLabel->setWordWrap(true);
    m_pluginDirLabel->setText(QStringLiteral("%1: %2").arg(
        tr("当前插件目录"), PluginManager::pluginsDir()));
    layout->addWidget(m_pluginDirLabel);

    m_pluginList = new QListWidget(page);
    m_pluginList->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_pluginList, 1);

    auto *row = new QHBoxLayout;
    auto *rescanBtn = new QPushButton(tr("重新扫描"), page);
    rescanBtn->setObjectName(QStringLiteral("ghostButton"));
    m_pluginRescanResult = new QLabel(page);
    m_pluginRescanResult->setObjectName(QStringLiteral("hintLabel"));
    m_pluginRescanResult->setWordWrap(true);
    row->addWidget(rescanBtn);
    row->addWidget(m_pluginRescanResult, 1);
    layout->addLayout(row);

    connect(rescanBtn, &QPushButton::clicked, this, [this, rescanBtn]() {
        // rescan 是同步阻塞调用（每个插件最多 4s 超时），用按钮禁用提示用户。
        rescanBtn->setEnabled(false);
        m_pluginRescanResult->setText(tr("扫描中…"));
        QCoreApplication::processEvents();

        const QVector<PluginInfo> plugins = PluginManager::instance().rescan();
        reloadPluginList();

        rescanBtn->setEnabled(true);
        const int total = plugins.size();
        const int ok = std::count_if(plugins.begin(), plugins.end(),
                                     [](const PluginInfo &p) { return p.available; });
        m_pluginRescanResult->setText(
            tr("发现 %1 个插件，%2 个可用").arg(total).arg(ok));
    });

    // 首次进入页面时显示当前快照（PluginManager 还没探测过则显示"暂无插件"），
    // 提示用户点"重新扫描"主动探测。这里不主动 rescan，避免每次打开设置窗时
    // 因扫插件目录阻塞若干秒（默认目录不存在时虽快，但环境变量目录可能慢）。
    reloadPluginList();
    if (PluginManager::instance().plugins().isEmpty())
        m_pluginRescanResult->setText(tr("点击「重新扫描」探测插件"));

    layout->addStretch(1);
    return page;
}

void SettingsWindow::reloadPluginList()
{
    if (!m_pluginList)
        return;
    m_pluginList->clear();
    m_pluginDirLabel->setText(QStringLiteral("%1: %2").arg(
        tr("当前插件目录"), PluginManager::pluginsDir()));

    const QVector<PluginInfo> plugins = PluginManager::instance().plugins();
    if (plugins.isEmpty()) {
        auto *item = new QListWidgetItem(tr("（暂无插件，请把插件子目录放入上述路径）"));
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        m_pluginList->addItem(item);
        return;
    }
    for (const PluginInfo &p : plugins) {
        // "echo  ✓ 可用  v0.0.1"  /  "echo  ✗ 不可用：start failed: ..."
        const QString status = p.available
            ? tr("✓ 可用  v%1").arg(p.version)
            : tr("✗ 不可用：%1").arg(p.error);
        auto *item = new QListWidgetItem(QStringLiteral("%1  %2").arg(p.name, status));
        if (!p.available)
            item->setForeground(QColor(0x9A, 0x92, 0x86));
        m_pluginList->addItem(item);
    }
}

// ---------------------------------------------------------------------------
// 关于
// ---------------------------------------------------------------------------
QWidget *SettingsWindow::buildAboutPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);
    layout->addWidget(makePageTitle(tr("关于"), page));

    auto *nameLabel = new QLabel(QStringLiteral("X翻译"), page);
    nameLabel->setObjectName(QStringLiteral("pageTitle"));
    auto *versionLabel = new QLabel(
        tr("版本 %1").arg(QApplication::applicationVersion()), page);

    auto *components = new QLabel(tr(
        "开源组件：\n"
        "· Qt 6（LGPL v3）\n"
        "· OpenCV（Apache-2.0）\n"
        "· ONNX Runtime（MIT）\n"
        "· PaddleOCR PP-OCRv6 模型（Apache-2.0）\n"
        "· Material 图标风格"), page);
    components->setWordWrap(true);

    auto *disclaimer = makeHint(tr(
        "免责声明：本软件为学习性复刻项目，仅供学习与技术交流使用，"
        "不用于任何商业用途。翻译结果由第三方服务提供，准确性以各服务商为准。"),
        page);

    layout->addWidget(nameLabel);
    layout->addWidget(versionLabel);
    layout->addSpacing(8);
    layout->addWidget(components);
    layout->addSpacing(8);
    layout->addWidget(disclaimer);
    layout->addStretch(1);
    return page;
}
