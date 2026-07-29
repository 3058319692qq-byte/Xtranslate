#include "core/storage/HistoryStore.h"

#include "core/config/ConfigManager.h"

#include <QAtomicInt>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

namespace {

QString defaultDbPath()
{
    QString base = qEnvironmentVariable("APPDATA");
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return QDir(base).filePath(QStringLiteral("XTranslate/history.db"));
}

HistoryEntry entryFromQuery(const QSqlQuery &q)
{
    HistoryEntry e;
    e.id = q.value(0).toLongLong();
    e.tsMs = q.value(1).toLongLong();
    e.srcLang = q.value(2).toString();
    e.dstLang = q.value(3).toString();
    e.srcText = q.value(4).toString();
    e.dstText = q.value(5).toString();
    e.provider = q.value(6).toString();
    e.scene = q.value(7).toString();
    e.favorite = q.value(8).toInt() != 0;
    return e;
}

} // namespace

HistoryStore &HistoryStore::instance()
{
    static HistoryStore store(defaultDbPath());
    return store;
}

HistoryStore::HistoryStore(const QString &dbPath, QObject *parent)
    : QObject(parent)
{
    // Each instance needs its own connection name (the test store and the
    // shared store may coexist in one process).
    static QAtomicInt counter;
    m_connectionName =
        QStringLiteral("xtranslate_history_%1").arg(counter.fetchAndAddRelaxed(1));

    QDir().mkpath(QFileInfo(dbPath).absolutePath());
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) {
        qWarning("HistoryStore: cannot open %s: %s", qPrintable(dbPath),
                 qPrintable(m_db.lastError().text()));
        return;
    }

    // 迁移期可能与其他残留连接竞争写锁，设 5s busy_timeout 避免 DROP TABLE
    // 立即 "database table is locked" 失败。
    QSqlQuery bt(m_db);
    bt.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));

    // 先确保表存在（首次创建用新 schema + user_version=1）。
    QSqlQuery ddl(m_db);
    bool created = ddl.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS trans_history("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " ts INTEGER NOT NULL,"
        " src_lang TEXT, dst_lang TEXT,"
        " src_text TEXT, dst_text TEXT,"
        " provider TEXT,"
        " scene TEXT CHECK(scene IN ('input','capture','selection','replace')),"
        " favorite INTEGER DEFAULT 0)"));

    // v0.7.1 BUG-B 根因修复：history.db 被外部写坏（非 SQLite 文件/截断，
    // 真机实际命中：旧验收脚本留下的 58 字节 marker 文件）时，此处
    // CREATE TABLE 报 "file is not a database" → 旧版 m_ok=false，之后每次
    // add() 静默返 0、query() 永远空 → "翻译多次历史却为空"。
    // 修复：与 ConfigManager 损坏回退同款策略——证据保全为 .bak_corrupt
    // 后重建空库，绝不让历史功能静默失效。
    if (!created) {
        qWarning("HistoryStore: schema init failed (%s), assuming corrupt db; "
                 "backing up and recreating",
                 qPrintable(ddl.lastError().text()));
        ddl.clear();
        bt.clear();
        m_db.close();
        const QString corruptBak = dbPath + QStringLiteral(".bak_corrupt");
        QFile::remove(corruptBak);
        if (QFile::rename(dbPath, corruptBak) && m_db.open()) {
            QSqlQuery bt2(m_db);
            bt2.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));
            QSqlQuery ddl2(m_db);
            created = ddl2.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS trans_history("
                " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                " ts INTEGER NOT NULL,"
                " src_lang TEXT, dst_lang TEXT,"
                " src_text TEXT, dst_text TEXT,"
                " provider TEXT,"
                " scene TEXT CHECK(scene IN "
                "('input','capture','selection','replace')),"
                " favorite INTEGER DEFAULT 0)"));
            if (created) {
                QSqlQuery v1(m_db);
                v1.exec(QStringLiteral("PRAGMA user_version = 1"));
                qWarning("HistoryStore: corrupt db backed up to %s, fresh db "
                         "recreated", qPrintable(corruptBak));
            }
        }
    }

    // Schema 迁移：user_version 0→1。老库 CHECK 不含 'replace'，需重建表。
    // 失败回滚，库保持 v0 可用；首次迁移前备份 .bak_v0。
    // PRAGMA user_version 在事务外执行（事务内执行在某些 SQLite 版本会回滚）。
    if (created) {
        // 检测阶段：所有 QSqlQuery 放到独立 block，确保 statement cursor
        // 析构后再进入迁移。否则 SELECT 持有的 SHARED 锁会让 DROP TABLE
        // 拿不到 EXCLUSIVE 锁，报 "database table is locked"。
        int version = -1;
        bool alreadyNewSchema = false;
        int rows = -1;
        {
            QSqlQuery pv(m_db);
            pv.exec(QStringLiteral("PRAGMA user_version"));
            if (pv.next())
                version = pv.value(0).toInt();

            QSqlQuery sm(m_db);
            if (sm.exec(QStringLiteral(
                    "SELECT sql FROM sqlite_master WHERE type='table' "
                    "AND name='trans_history'"))
                && sm.next()) {
                alreadyNewSchema = sm.value(0).toString().contains(
                    QStringLiteral("'replace'"), Qt::CaseInsensitive);
            }

            QSqlQuery cnt(m_db);
            cnt.exec(QStringLiteral("SELECT COUNT(*) FROM trans_history"));
            if (cnt.next())
                rows = cnt.value(0).toInt();
        }  // pv/sm/cnt 析构，释放 statement cursor 与 SHARED 锁

        if (version == 0) {
            if (alreadyNewSchema) {
                // 表已是新 schema，仅补 user_version。
                QSqlQuery v1(m_db);
                v1.exec(QStringLiteral("PRAGMA user_version = 1"));
            } else if (rows > 0) {
                // 真正迁移：备份 + 分段事务重建表。
                // 注意：SQLite 同一事务内 INSERT...SELECT 会对老表加共享锁，
                // 随后 DROP TABLE 需写锁，与自身共享锁冲突导致
                // "database table is locked"。因此拆成两段事务 + 每段用独立
                // QSqlQuery 作用域，确保 statement cursor 析构释放表锁。
                const QString bak = dbPath + QStringLiteral(".bak_v0");
                if (!QFile::exists(bak))
                    QFile::copy(dbPath, bak);

                bool ok = true;

                // ---- 事务1：建 v1 + 复制数据 ----
                {
                    QSqlQuery q(m_db);
                    if (!m_db.transaction()) {
                        qWarning("HistoryStore: migrate begin1 failed: %s",
                                 qPrintable(m_db.lastError().text()));
                        ok = false;
                    } else {
                        if (!q.exec(QStringLiteral(
                                "CREATE TABLE trans_history_v1("
                                " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                " ts INTEGER NOT NULL,"
                                " src_lang TEXT, dst_lang TEXT,"
                                " src_text TEXT, dst_text TEXT,"
                                " provider TEXT,"
                                " scene TEXT CHECK(scene IN "
                                "('input','capture','selection','replace')),"
                                " favorite INTEGER DEFAULT 0)"))) {
                            qWarning("HistoryStore: migrate CREATE v1 failed: %s",
                                     qPrintable(q.lastError().text()));
                            ok = false;
                        }
                        if (ok && !q.exec(QStringLiteral(
                                "INSERT INTO trans_history_v1(id,ts,src_lang,"
                                "dst_lang,src_text,dst_text,provider,scene,"
                                "favorite) SELECT id,ts,src_lang,dst_lang,"
                                "src_text,dst_text,provider,scene,favorite "
                                "FROM trans_history"))) {
                            qWarning("HistoryStore: migrate INSERT v1 failed: %s",
                                     qPrintable(q.lastError().text()));
                            ok = false;
                        }
                        if (ok) {
                            if (!m_db.commit()) {
                                qWarning("HistoryStore: migrate commit1 failed: %s",
                                         qPrintable(m_db.lastError().text()));
                                m_db.rollback();
                                ok = false;
                            }
                        } else {
                            m_db.rollback();
                        }
                    }
                }  // q 析构，释放 INSERT...SELECT 的 statement cursor

                // ---- 独立 DROP old（事务外，新 QSqlQuery）----
                if (ok) {
                    QSqlQuery q(m_db);
                    if (!q.exec(
                            QStringLiteral("DROP TABLE trans_history"))) {
                        qWarning("HistoryStore: migrate DROP old failed: %s",
                                 qPrintable(q.lastError().text()));
                        ok = false;
                    }
                }

                // ---- 事务2：RENAME + INDEX ----
                if (ok) {
                    QSqlQuery q(m_db);
                    if (!m_db.transaction()) {
                        qWarning("HistoryStore: migrate begin2 failed: %s",
                                 qPrintable(m_db.lastError().text()));
                        ok = false;
                    } else {
                        if (!q.exec(QStringLiteral(
                                "ALTER TABLE trans_history_v1 RENAME TO "
                                "trans_history"))) {
                            qWarning("HistoryStore: migrate RENAME failed: %s",
                                     qPrintable(q.lastError().text()));
                            ok = false;
                        }
                        if (ok && !q.exec(QStringLiteral(
                                "CREATE INDEX IF NOT EXISTS idx_hist_ts ON "
                                "trans_history(ts)"))) {
                            qWarning("HistoryStore: migrate IDX ts failed: %s",
                                     qPrintable(q.lastError().text()));
                            ok = false;
                        }
                        if (ok && !q.exec(QStringLiteral(
                                "CREATE INDEX IF NOT EXISTS idx_hist_fav ON "
                                "trans_history(favorite)"))) {
                            qWarning("HistoryStore: migrate IDX fav failed: %s",
                                     qPrintable(q.lastError().text()));
                            ok = false;
                        }
                        if (ok) {
                            if (!m_db.commit()) {
                                qWarning("HistoryStore: migrate commit2 failed: %s",
                                         qPrintable(m_db.lastError().text()));
                                m_db.rollback();
                                ok = false;
                            }
                        } else {
                            m_db.rollback();
                        }
                    }
                }

                if (ok) {
                    // 全部成功后设 user_version（事务外）。
                    QSqlQuery v1(m_db);
                    if (!v1.exec(QStringLiteral("PRAGMA user_version = 1")))
                        qWarning("HistoryStore: user_version=1 failed: %s",
                                 qPrintable(v1.lastError().text()));
                } else {
                    qWarning("HistoryStore: migrate rolled back, db stays v0");
                }
            } else {
                // 空新库直接标 v1。
                QSqlQuery v1(m_db);
                v1.exec(QStringLiteral("PRAGMA user_version = 1"));
            }
        }
    }

    m_ok = created;
    if (m_ok) {
        // 重建路径后 ddl 可能挂在已关闭的连接上，索引用新 QSqlQuery。
        QSqlQuery idx(m_db);
        idx.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_hist_ts ON trans_history(ts)"));
        idx.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_hist_fav ON trans_history(favorite)"));
    }
}

HistoryStore::~HistoryStore()
{
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase(); // release before removeDatabase
    QSqlDatabase::removeDatabase(m_connectionName);
}

int HistoryStore::effectiveLimit() const
{
    if (m_limitOverride > 0)
        return m_limitOverride;
    const int limit = ConfigManager::instance().historyLimit();
    return limit > 0 ? limit : 5000;
}

qint64 HistoryStore::add(const HistoryEntry &entry)
{
    if (!m_ok)
        return 0;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO trans_history"
        " (ts, src_lang, dst_lang, src_text, dst_text, provider, scene, favorite)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(entry.tsMs > 0 ? entry.tsMs
                                  : QDateTime::currentMSecsSinceEpoch());
    q.addBindValue(entry.srcLang);
    q.addBindValue(entry.dstLang);
    q.addBindValue(entry.srcText);
    q.addBindValue(entry.dstText);
    q.addBindValue(entry.provider);
    q.addBindValue(entry.scene);
    q.addBindValue(entry.favorite ? 1 : 0);
    if (!q.exec()) {
        qWarning("HistoryStore: insert failed: %s",
                 qPrintable(q.lastError().text()));
        return 0;
    }
    const qint64 id = q.lastInsertId().toLongLong();
    trim();
    emit changed();
    return id;
}

int HistoryStore::trim()
{
    const int overflow = count() - effectiveLimit();
    if (overflow <= 0)
        return 0;
    // Drop the OLDEST non-favorite rows; favorites are never trimmed.
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "DELETE FROM trans_history WHERE id IN ("
        " SELECT id FROM trans_history WHERE favorite = 0"
        " ORDER BY ts ASC, id ASC LIMIT ?)"));
    q.addBindValue(overflow);
    if (!q.exec())
        return 0;
    return q.numRowsAffected();
}

QList<HistoryEntry> HistoryStore::query(const QString &like, bool favoriteOnly,
                                        int max) const
{
    QList<HistoryEntry> list;
    if (!m_ok)
        return list;

    QString sql = QStringLiteral(
        "SELECT id, ts, src_lang, dst_lang, src_text, dst_text, provider,"
        " scene, favorite FROM trans_history");
    QStringList where;
    if (!like.trimmed().isEmpty())
        where << QStringLiteral("(src_text LIKE ? OR dst_text LIKE ?)");
    if (favoriteOnly)
        where << QStringLiteral("favorite = 1");
    if (!where.isEmpty())
        sql += QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));
    sql += QStringLiteral(" ORDER BY ts DESC, id DESC LIMIT ?");

    QSqlQuery q(m_db);
    q.prepare(sql);
    if (!like.trimmed().isEmpty()) {
        const QString pattern = QLatin1Char('%') + like.trimmed() + QLatin1Char('%');
        q.addBindValue(pattern);
        q.addBindValue(pattern);
    }
    q.addBindValue(max);
    if (!q.exec())
        return list;
    while (q.next())
        list.append(entryFromQuery(q));
    return list;
}

bool HistoryStore::setFavorite(qint64 id, bool favorite)
{
    if (!m_ok)
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE trans_history SET favorite = ? WHERE id = ?"));
    q.addBindValue(favorite ? 1 : 0);
    q.addBindValue(id);
    const bool ok = q.exec() && q.numRowsAffected() > 0;
    if (ok)
        emit changed();
    return ok;
}

bool HistoryStore::remove(qint64 id)
{
    if (!m_ok)
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM trans_history WHERE id = ?"));
    q.addBindValue(id);
    const bool ok = q.exec() && q.numRowsAffected() > 0;
    if (ok)
        emit changed();
    return ok;
}

int HistoryStore::clearNonFavorites()
{
    if (!m_ok)
        return 0;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("DELETE FROM trans_history WHERE favorite = 0")))
        return 0;
    const int removed = q.numRowsAffected();
    if (removed > 0)
        emit changed();
    return removed;
}

int HistoryStore::count() const
{
    if (!m_ok)
        return 0;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM trans_history")) || !q.next())
        return 0;
    return q.value(0).toInt();
}
