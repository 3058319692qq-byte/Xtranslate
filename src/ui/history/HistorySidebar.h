// HistorySidebar - collapsible translation-history dock (phase 4).
//
// QDockWidget hosting: search box (fuzzy src/dst match) + 只看收藏 toggle +
// 清空历史 button + entry list (time / scene icon / source & result
// snippets, ★ for favorites). Double-click emits entryActivated so the main
// window can refill the input box; the context menu offers favorite /
// unfavorite / copy result / delete. Auto-refreshes on HistoryStore::changed.

#pragma once

#include "core/storage/HistoryStore.h"

#include <QDockWidget>

class QCheckBox;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTimer;

class HistorySidebar : public QDockWidget
{
    Q_OBJECT
public:
    explicit HistorySidebar(QWidget *parent = nullptr);

signals:
    // Double-click: hand the entry back to the main window.
    void entryActivated(const HistoryEntry &entry);

private slots:
    void refresh();
    void showContextMenu(const QPoint &pos);

private:
    static QIcon sceneIcon(const QString &scene);
    HistoryEntry entryForItem(QListWidgetItem *item) const;

    QLineEdit *m_searchEdit = nullptr;
    QCheckBox *m_favOnlyCheck = nullptr;
    QPushButton *m_clearButton = nullptr;
    QListWidget *m_list = nullptr;
    QTimer *m_refreshDebounce = nullptr;
    QList<HistoryEntry> m_entries; // parallel to list rows
};
