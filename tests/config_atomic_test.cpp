// config_atomic_test - standalone ctest target (console subsystem).
//
// Covers one core path in isolation: src/core/config ConfigManager
//   A) fresh create: defaults written atomically to an isolated temp dir;
//   B) setValue persists immediately and leaves no QSaveFile temp residue;
//   C) corrupt file -> evidence kept as config.json.bak, defaults restored,
//      loadedFromBackup() reports the recovery.
//
// Deliberately NOT part of the XTranslate WIN32 exe: the --selftest contract
// (AttachConsole + one JSON line + exit code) stays untouched. Same output
// convention though: one JSON line on stdout, exit 0 = all checks pass.

#include "core/config/ConfigManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdio>

namespace {

int g_checks = 0;
QJsonArray g_failed;

void check(bool ok, const char *label)
{
    ++g_checks;
    if (!ok)
        g_failed.append(QString::fromLatin1(label));
}

// Raw re-read from disk (bypasses ConfigManager) to verify what was persisted.
QJsonObject readJsonFile(const QString &path, bool *parsedOk)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *parsedOk = false;
        return {};
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    *parsedOk = (err.error == QJsonParseError::NoError && doc.isObject());
    return doc.object();
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::fprintf(stdout, "{\"test\":\"config_atomic\",\"ok\":false,"
                             "\"error\":\"QTemporaryDir invalid\"}\n");
        return 2;
    }
    const QString cfgPath = QDir(dir.path()).filePath(QStringLiteral("config.json"));
    const QString bakPath = cfgPath + QStringLiteral(".bak");

    // ---- A) fresh create: defaults land on disk, no recovery flagged ----
    {
        ConfigManager mgr(cfgPath);
        check(QFile::exists(cfgPath), "A_file_created");
        check(!mgr.loadedFromBackup(), "A_not_recovered");
        bool parsed = false;
        const QJsonObject onDisk = readJsonFile(cfgPath, &parsed);
        check(parsed, "A_file_parses");
        check(onDisk.value(QStringLiteral("version")).toInt() == 4, "A_version_4");
        check(onDisk == ConfigManager::defaults(), "A_matches_defaults");

        // ---- B) setValue persists atomically, no temp residue ----
        mgr.setValue(QStringLiteral("theme"), QStringLiteral("dark"));
        const QJsonObject after = readJsonFile(cfgPath, &parsed);
        check(parsed, "B_file_parses");
        check(after.value(QStringLiteral("theme")).toString()
                  == QLatin1String("dark"), "B_theme_persisted");
        // QSaveFile temp (config.json.XXXXXX) must be gone after commit.
        const QStringList entries = QDir(dir.path())
            .entryList(QDir::Files | QDir::Hidden | QDir::System);
        check(entries == QStringList{QStringLiteral("config.json")},
              "B_no_temp_residue");
    }

    // ---- C) corruption fallback: .bak evidence + defaults + flag ----
    const QByteArray garbage("{ this is not json !!");
    {
        QFile f(cfgPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            std::fprintf(stdout, "{\"test\":\"config_atomic\",\"ok\":false,"
                                 "\"error\":\"cannot corrupt config.json\"}\n");
            return 2;
        }
        f.write(garbage);
    }
    {
        ConfigManager mgr(cfgPath);
        check(mgr.loadedFromBackup(), "C_recovery_flagged");
        // Evidence preserved verbatim.
        QFile bak(bakPath);
        check(bak.open(QIODevice::ReadOnly) && bak.readAll() == garbage,
              "C_bak_keeps_evidence");
        // config.json rewritten as valid defaults (dark from B is gone).
        bool parsed = false;
        const QJsonObject onDisk = readJsonFile(cfgPath, &parsed);
        check(parsed, "C_rewritten_parses");
        check(onDisk.value(QStringLiteral("theme")).toString()
                  == QLatin1String("system"), "C_defaults_restored");
        check(mgr.value(QStringLiteral("version")).toInt() == 4, "C_version_4");
    }

    QJsonObject result;
    result.insert(QStringLiteral("test"), QStringLiteral("config_atomic"));
    result.insert(QStringLiteral("checks"), g_checks);
    result.insert(QStringLiteral("failed"), g_failed);
    result.insert(QStringLiteral("ok"), g_failed.isEmpty());
    std::fprintf(stdout, "%s\n",
                 QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
    return g_failed.isEmpty() ? 0 : 1;
}
