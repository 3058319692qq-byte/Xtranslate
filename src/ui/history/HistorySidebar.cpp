#include "ui/history/HistorySidebar.h"

#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int kSnippetLen = 24;

QString snippet(const QString &text)
{
    QString flat = text;
    flat.replace(QLatin1Char('\n'), QLatin1Char(' '));
    if (flat.size() > kSnippetLen)
        return flat.left(kSnippetLen) + QStringLiteral("…");
    return flat;
}

} // namespace

HistorySidebar::HistorySidebar(QWidget *parent)
    : QDockWidget(parent)
{
    setObjectName(QStringLiteral("historyDock"));
    setWindowTitle(tr("历史记录"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);

    auto *body = new QWidget(this);
    // Phase 7: 新增 objectName 用于 QSS 透明背景定位，让 MainWindow 的 Mica
    // 透过 dock + body，QListWidget 的 glass_surface alpha 才能真正生效。
    body->setObjectName(QStringLiteral("historyBody"));
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // top row: favorites-only toggle + clear
    auto *topRow = new QHBoxLayout;
    m_favOnlyCheck = new QCheckBox(tr("只看收藏"), body);
    m_clearButton = new QPushButton(tr("清空历史"), body);
    m_clearButton->setObjectName(QStringLiteral("ghostButton"));
    topRow->addWidget(m_favOnlyCheck);
    topRow->addStretch(1);
    topRow->addWidget(m_clearButton);
    layout->addLayout(topRow);

    m_searchEdit = new QLineEdit(body);
    m_searchEdit->setPlaceholderText(tr("搜索原文/译文…"));
    m_searchEdit->setClearButtonEnabled(true);
    layout->addWidget(m_searchEdit);

    m_list = new QListWidget(body);
    m_list->setObjectName(QStringLiteral("historyList"));
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setWordWrap(true);
    layout->addWidget(m_list, 1);

    setWidget(body);

    // small debounce so typing in the search box does not hammer SQLite
    m_refreshDebounce = new QTimer(this);
    m_refreshDebounce->setSingleShot(true);
    m_refreshDebounce->setInterval(200);
    connect(m_refreshDebounce, &QTimer::timeout, this, &HistorySidebar::refresh);

    connect(m_searchEdit, &QLineEdit::textChanged,
            m_refreshDebounce, qOverload<>(&QTimer::start));
    connect(m_favOnlyCheck, &QCheckBox::toggled,
            this, &HistorySidebar::refresh);
    connect(m_clearButton, &QPushButton::clicked, this, [this]() {
        const auto answer = QMessageBox::question(
            this, tr("清空历史"),
            tr("确定清空所有历史记录吗？收藏的记录会保留。"));
        if (answer == QMessageBox::Yes)
            HistoryStore::instance().clearNonFavorites();
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) {
        const HistoryEntry entry = entryForItem(item);
        if (entry.id > 0)
            emit entryActivated(entry);
    });
    connect(m_list, &QListWidget::customContextMenuRequested,
            this, &HistorySidebar::showContextMenu);
    connect(&HistoryStore::instance(), &HistoryStore::changed,
            this, &HistorySidebar::refresh);

    refresh();
}

QIcon HistorySidebar::sceneIcon(const QString &scene)
{
    // Phase 7-fix1：去橙改灰阶，三场景统一用 subText 深灰底 + 白字 glyph，
    // 视觉区分靠 glyph 本身（⌨ input / ▣ capture / ✍ selection），
    // 不再依赖彩色背景。符合"UI 一律无彩色"原则。
    QChar glyph = QChar(0x2328);                    // ⌨ input
    if (scene == QLatin1String("capture")) {
        glyph = QChar(0x25A3);                      // ▣ capture
    } else if (scene == QLatin1String("selection")) {
        glyph = QChar(0x270D);                      // ✍ selection
    }
    const QColor bg = ThemeManager::instance().palette().subText;

    QPixmap pm(18, 18);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(0, 0, 18, 18), 4, 4);
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setPixelSize(11);
    p.setFont(f);
    p.drawText(QRectF(0, 0, 18, 18), Qt::AlignCenter, QString(glyph));
    p.end();
    return QIcon(pm);
}

void HistorySidebar::refresh()
{
    m_entries = HistoryStore::instance().query(m_searchEdit->text(),
                                               m_favOnlyCheck->isChecked());
    m_list->clear();
    // v0.7.1 BUG-B：空态占位。mock 离线占位结果按策略不入库，新用户网络
    // 不通时侧栏永远空白会被误认为"历史坏了"，用占位文案说明原因。
    // 占位项 NoItemFlags：不可选/不响应双击，右键菜单经 entryForItem
    // id<=0 早退，零副作用。
    if (m_entries.isEmpty()) {
        const bool filtered = !m_searchEdit->text().isEmpty()
                              || m_favOnlyCheck->isChecked();
        auto *placeholder = new QListWidgetItem(
            filtered
                ? tr("无匹配的历史记录")
                : tr("暂无历史记录\n（翻译成功后自动记录；"
                     "离线占位结果不计入）"));
        placeholder->setFlags(Qt::NoItemFlags);
        placeholder->setTextAlignment(Qt::AlignCenter);
        m_list->addItem(placeholder);
        return;
    }
    for (const HistoryEntry &e : m_entries) {
        const QString when = QDateTime::fromMSecsSinceEpoch(e.tsMs)
                                 .toString(QStringLiteral("MM-dd HH:mm"));
        const QString star = e.favorite ? QStringLiteral("★ ") : QString();
        auto *item = new QListWidgetItem(
            sceneIcon(e.scene),
            QStringLiteral("%1%2\n%3 → %4")
                .arg(star, when, snippet(e.srcText), snippet(e.dstText)));
        item->setData(Qt::UserRole, e.id);
        item->setToolTip(tr("原文：%1\n译文：%2\n服务：%3")
                             .arg(e.srcText, e.dstText, e.provider));
        m_list->addItem(item);
    }
}

HistoryEntry HistorySidebar::entryForItem(QListWidgetItem *item) const
{
    if (!item)
        return HistoryEntry();
    const qint64 id = item->data(Qt::UserRole).toLongLong();
    for (const HistoryEntry &e : m_entries) {
        if (e.id == id)
            return e;
    }
    return HistoryEntry();
}

void HistorySidebar::showContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_list->itemAt(pos);
    if (!item)
        return;
    const HistoryEntry entry = entryForItem(item);
    if (entry.id <= 0)
        return;

    QMenu menu(this);
    QAction *favAction = menu.addAction(
        entry.favorite ? tr("取消收藏") : tr("收藏"));
    QAction *copyAction = menu.addAction(tr("复制译文"));
    QAction *deleteAction = menu.addAction(tr("删除"));

    QAction *chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
    if (chosen == favAction) {
        HistoryStore::instance().setFavorite(entry.id, !entry.favorite);
    } else if (chosen == copyAction) {
        QApplication::clipboard()->setText(entry.dstText);
    } else if (chosen == deleteAction) {
        HistoryStore::instance().remove(entry.id);
    }
}
