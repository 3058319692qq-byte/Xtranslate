// ConfigManager - JSON application configuration (phase 4).
//
// Singleton persisting to %APPDATA%\XTranslate\config.json. Every set*()
// saves immediately (atomic: QSaveFile writes a temp file and renames) and
// emits configChanged(key) so hotkeys/theme/proxy listeners can react live.
//
// Corruption handling: when the file exists but does not parse, it is copied
// aside as config.json.bak and the defaults are used (never crashes).
//
// A second constructor taking an explicit file path exists for
// --selftest config, which must not touch the real %APPDATA% tree.

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

class ConfigManager : public QObject
{
    Q_OBJECT
public:
    static ConfigManager &instance();

    // Test constructor: explicit config file path (QTemporaryDir isolation).
    explicit ConfigManager(const QString &filePath, QObject *parent = nullptr);

    // Dotted-path accessors over the underlying JSON document, e.g.
    // value("proxy.mode"), setValue("providers.deepl.apiKey", "..").
    QJsonValue value(const QString &path) const;
    void setValue(const QString &path, const QJsonValue &val);

    QString stringValue(const QString &path) const { return value(path).toString(); }
    bool boolValue(const QString &path) const { return value(path).toBool(); }
    int intValue(const QString &path) const { return value(path).toInt(); }

    // ---- typed convenience (all read the live JSON) ----
    QString uiLanguage() const   { return stringValue(QStringLiteral("ui_language")); }
    QString theme() const        { return stringValue(QStringLiteral("theme")); }
    // Phase 7：减少透明度开关（Liquid Glass 玻璃面退化为不透明卡片主题）。
    // 默认关，开启或系统不支持 backdrop 时全部玻璃窗口统一退化。
    bool reduceTransparency() const { return boolValue(QStringLiteral("ui.reduce_transparency")); }
    QString ocrEngine() const    { return stringValue(QStringLiteral("ocr.engine")); }
    bool ttsEnabled() const      { return boolValue(QStringLiteral("tts.enabled")); }
    bool selectionEnabled() const{ return boolValue(QStringLiteral("selection.enabled")); }
    bool autostart() const       { return boolValue(QStringLiteral("autostart")); }
    int historyLimit() const     { return intValue(QStringLiteral("history.limit")); }

    // ---- 通知开关（phase 5 + phase 6-tuning）----
    // 总开关 + 场景类目开关；总开关关=全关，逐项在总开关开时生效；默认全开。
    // category ∈ {"capture_ocr","capture_translate","selection","replace",
    //              "translate_failed"}。
    bool notificationsEnabled() const;
    bool notificationCategoryEnabled(const QString &category) const;

    // Hotkey mapping actionId -> key sequence string (defaults merged in).
    QString hotkeyFor(const QString &actionId) const;
    void setHotkey(const QString &actionId, const QString &keySeq);

    // Provider priority order (UI-facing; never contains "mock").
    QStringList providerOrder() const;
    void setProviderOrder(const QStringList &order);
    QJsonObject providerConfig(const QString &name) const;
    void setProviderField(const QString &name, const QString &field,
                          const QJsonValue &val);

    QString configFilePath() const { return m_filePath; }
    bool loadedFromBackup() const { return m_recoveredFromCorrupt; }

    // Full default document (also used by the self-test to compare).
    static QJsonObject defaults();

signals:
    // Emitted once per setValue with the dotted path that changed.
    void configChanged(const QString &path);

private:
    void load();
    bool save() const;

    QString m_filePath;
    QJsonObject m_root;
    bool m_recoveredFromCorrupt = false;
};
