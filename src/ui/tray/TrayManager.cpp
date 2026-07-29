#include "ui/tray/TrayManager.h"

#include "core/config/ConfigManager.h"
#include "core/hotkey/HotkeyManager.h"

#include <QAction>
#include <QFont>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSystemTrayIcon>

namespace {

// Phase 7-fix1：去橙改深灰 #202020 圆角方块 + 白色"译"字。
// 浅深任务栏都清晰（深色任务栏白字显眼，浅色任务栏深灰底可辨）。
QPixmap paintIconPixmap(int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    const qreal radius = size * 0.22;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x20, 0x20, 0x20));
    p.drawRoundedRect(QRectF(0, 0, size, size), radius, radius);

    QFont font;
    font.setFamilies({QStringLiteral("Microsoft YaHei"), QStringLiteral("SimHei")});
    font.setPixelSize(static_cast<int>(size * 0.62));
    font.setBold(true);
    p.setFont(font);
    p.setPen(Qt::white);
    p.drawText(QRectF(0, -size * 0.02, size, size), Qt::AlignCenter,
               QStringLiteral("译"));
    p.end();
    return pm;
}

} // namespace

TrayManager::TrayManager(QObject *parent)
    : QObject(parent)
{
    m_menu = new QMenu;

    // The five hotkey actions, same order/labels as HotkeyManager. Shortcuts
    // are display-only (WidgetShortcut never triggers from a QMenu), the
    // actual global trigger is RegisterHotKey.
    struct Wiring { const char *actionId; void (TrayManager::*signal)(); };
    const Wiring wirings[] = {
        {"screenshot_translate", &TrayManager::screenshotTranslateRequested},
        {"screenshot_ocr", &TrayManager::screenshotOcrRequested},
        {"selection_translate", &TrayManager::selectionTranslateRequested},
        {"toggle_main", &TrayManager::toggleMainWindowRequested},
        {"speak_clipboard", &TrayManager::speakClipboardRequested},
    };
    const QVector<HotkeyManager::ActionSpec> specs = HotkeyManager::defaultActions();
    for (const Wiring &wiring : wirings) {
        for (const HotkeyManager::ActionSpec &spec : specs) {
            if (spec.actionId != QLatin1String(wiring.actionId))
                continue;
            QAction *action = m_menu->addAction(spec.label);
            action->setShortcutContext(Qt::WidgetShortcut);
            action->setShortcutVisibleInContextMenu(true);
            connect(action, &QAction::triggered, this, wiring.signal);
            m_actionById.insert(spec.actionId, action);
            break;
        }
    }
    refreshShortcuts();
    // Rebinds from the settings page land in config("hotkeys.*").
    connect(&ConfigManager::instance(), &ConfigManager::configChanged, this,
            [this](const QString &path) {
        if (path.startsWith(QLatin1String("hotkeys")))
            refreshShortcuts();
    });

    m_menu->addSeparator();
    m_settingsAction = m_menu->addAction(tr("设置"));
    connect(m_settingsAction, &QAction::triggered,
            this, &TrayManager::settingsRequested);
    QAction *quitAction = m_menu->addAction(tr("退出"));
    connect(quitAction, &QAction::triggered, this, &TrayManager::quitRequested);

    m_tray = new QSystemTrayIcon(appIcon(), this);
    m_tray->setToolTip(QStringLiteral("X翻译"));
    m_tray->setContextMenu(m_menu);
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick)
            emit toggleMainWindowRequested();
    });
    m_tray->show();
}

TrayManager::~TrayManager()
{
    // QMenu is a parentless top-level widget (TrayManager is not a QWidget).
    delete m_menu;
}

QIcon TrayManager::appIcon()
{
    QIcon icon;
    icon.addPixmap(paintIconPixmap(16));
    icon.addPixmap(paintIconPixmap(24));
    icon.addPixmap(paintIconPixmap(32));
    icon.addPixmap(paintIconPixmap(48));
    icon.addPixmap(paintIconPixmap(64));
    icon.addPixmap(paintIconPixmap(256));
    return icon;
}

void TrayManager::refreshShortcuts()
{
    // Display-only (WidgetShortcut): mirror the configured bindings.
    ConfigManager &cfg = ConfigManager::instance();
    const QVector<HotkeyManager::ActionSpec> specs = HotkeyManager::defaultActions();
    for (const HotkeyManager::ActionSpec &spec : specs) {
        QAction *action = m_actionById.value(spec.actionId);
        if (!action)
            continue;
        const QString saved = cfg.hotkeyFor(spec.actionId);
        action->setShortcut(saved.isEmpty() ? spec.sequence
                                            : QKeySequence(saved));
    }
}

bool TrayManager::isAvailable() const
{
    return QSystemTrayIcon::isSystemTrayAvailable() && m_tray->isVisible();
}

void TrayManager::showBubble(const QString &title, const QString &message,
                             const QString &category)
{
    // 空分类（系统错误/欢迎提示）绕过开关；其他走 notifications 配置。
    if (!category.isEmpty()
        && !ConfigManager::instance().notificationCategoryEnabled(category))
        return;
    m_tray->showMessage(title, message, appIcon(), 4000);
}
