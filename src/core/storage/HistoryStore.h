// HistoryStore - SQLite translation history (phase 4; scene 迁移于 phase 5).
//
// Default database: %APPDATA%\XTranslate\history.db (QSQLITE). Schema:
//   trans_history(id INTEGER PK AUTOINCREMENT, ts, src_lang, dst_lang,
//                 src_text, dst_text, provider,
//                 scene TEXT CHECK(scene IN
//                                  ('input','capture','selection','replace')),
//                 favorite INTEGER DEFAULT 0)
//   + index on (ts) and (favorite).
//
// Phase 5 迁移：user_version 0→1。老库 CHECK 不含 'replace'，打开时单事务
// 重建表（CREATE v1 + INSERT...SELECT + DROP + RENAME + 重建索引），失败回滚
// 库保持 v0 可用；首次迁移前备份 history.db.bak_v0（仅一次，不覆盖）。
// 空新库直接 user_version=1。
//
// add() trims the table afterwards: when the row count exceeds the limit
// (ConfigManager history.limit for the shared instance, or setLimit() for
// tests), the OLDEST rows with favorite=0 are deleted; favorites survive.
//
// The explicit-path constructor exists for --selftest db (temp database).

#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QSqlDatabase>
#include <QString>

struct HistoryEntry {
    qint64 id = 0;
    qint64 tsMs = 0;             // msecs since epoch
    QString srcLang;
    QString dstLang;
    QString srcText;
    QString dstText;
    QString provider;
    QString scene;               // "input" / "capture" / "selection"
    bool favorite = false;
};

class HistoryStore : public QObject
{
    Q_OBJECT
public:
    static HistoryStore &instance();

    explicit HistoryStore(const QString &dbPath, QObject *parent = nullptr);
    ~HistoryStore() override;

    bool isOpen() const { return m_ok; }

    // Inserts and trims; returns the new row id (0 on failure).
    qint64 add(const HistoryEntry &entry);

    // like: fuzzy match against src_text/dst_text (empty = all).
    QList<HistoryEntry> query(const QString &like, bool favoriteOnly,
                              int max = 500) const;

    bool setFavorite(qint64 id, bool favorite);
    bool remove(qint64 id);
    int clearNonFavorites();     // 清空历史 keeps favorites; returns rows removed
    int count() const;

    // Row cap override for tests; <= 0 falls back to the config value.
    void setLimit(int limit) { m_limitOverride = limit; }

signals:
    void changed();              // fired after any successful mutation

private:
    int effectiveLimit() const;
    int trim();                  // returns rows removed

    QSqlDatabase m_db;
    QString m_connectionName;
    bool m_ok = false;
    int m_limitOverride = 0;
};
