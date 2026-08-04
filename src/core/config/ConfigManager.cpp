#include "core/config/ConfigManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

// %APPDATA%\XTranslate\config.json (spec-fixed path, independent of the
// QStandardPaths org/app nesting).
QString defaultConfigPath()
{
    QString base = qEnvironmentVariable("APPDATA");
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return QDir(base).filePath(QStringLiteral("XTranslate/config.json"));
}

// Walks `path` ("a.b.c") into nested objects; returns the leaf value.
QJsonValue lookup(const QJsonObject &root, const QString &path)
{
    const QStringList parts = path.split(QLatin1Char('.'));
    QJsonValue cur = root;
    for (const QString &part : parts) {
        if (!cur.isObject())
            return QJsonValue();
        cur = cur.toObject().value(part);
    }
    return cur;
}

// Immutable-update helper: returns `obj` with path set to val, creating
// intermediate objects as needed.
QJsonObject withValue(QJsonObject obj, const QStringList &parts, int idx,
                      const QJsonValue &val)
{
    const QString key = parts.at(idx);
    if (idx == parts.size() - 1) {
        obj.insert(key, val);
        return obj;
    }
    QJsonObject child = obj.value(key).toObject();
    obj.insert(key, withValue(child, parts, idx + 1, val));
    return obj;
}

QJsonObject providerDefault(bool enabled)
{
    QJsonObject o;
    o.insert(QStringLiteral("enabled"), enabled);
    return o;
}

} // namespace

ConfigManager &ConfigManager::instance()
{
    static ConfigManager mgr(defaultConfigPath());
    return mgr;
}

ConfigManager::ConfigManager(const QString &filePath, QObject *parent)
    : QObject(parent)
    , m_filePath(filePath)
{
    load();
}

QJsonObject ConfigManager::defaults()
{
    QJsonObject root;
    // 版本号：每次配置 schema 变更 +1，由 load() 内的迁移逻辑消费。
    // v2：proxy.mode 默认从 none 改为 system，并新增 proxy.user_touched 标记。
    // v3：新增 ui.reduce_transparency（Liquid Glass 可达性开关，默认关）。
    //     纯字段新增，无语义改写，老配置自动补字段。
    // v4（Phase 7-fix1）：新增 ui.font 子对象（source_pt/result_pt/result_color/
    //   result_color_custom）。纯字段新增，无语义改写，老配置自动补字段。
    //   默认 pt 值取原 QSS 14px @ 96dpi 的换算值（≈10.5pt），取整为 11pt。
    root.insert(QStringLiteral("version"), 4);
    root.insert(QStringLiteral("ui_language"), QStringLiteral("zh_CN"));
    root.insert(QStringLiteral("theme"), QStringLiteral("system"));
    root.insert(QStringLiteral("autostart"), false);

    // Phase 7：Liquid Glass 可达性开关。开启或系统不支持 backdrop 时
    // 所有真/假玻璃面退化为不透明卡片（沿用 light/dark 面板色）。
    QJsonObject ui;
    ui.insert(QStringLiteral("reduce_transparency"), false);

    // Phase 7-fix1：翻译字体可调。QSS 模板移除 QPlainTextEdit/QTextEdit 的
    // 固定 font-size，由 ConfigManager + setFont() 驱动。
    // - source_pt/result_pt：9-28pt，默认 11pt（≈原 QSS 14px）
    // - result_color："theme"（跟随主题 textOnGlass，默认）或 "custom"
    // - result_color_custom：自选色，仅当 result_color="custom" 时生效
    QJsonObject font;
    font.insert(QStringLiteral("source_pt"), 11);
    font.insert(QStringLiteral("result_pt"), 11);
    font.insert(QStringLiteral("result_color"), QStringLiteral("theme"));
    font.insert(QStringLiteral("result_color_custom"), QStringLiteral("#1F2937"));
    ui.insert(QStringLiteral("font"), font);
    root.insert(QStringLiteral("ui"), ui);

    // Hotkeys: actionId -> portable key sequence (six default bindings).
    QJsonObject hotkeys;
    hotkeys.insert(QStringLiteral("screenshot_translate"), QStringLiteral("Alt+D"));
    hotkeys.insert(QStringLiteral("screenshot_ocr"), QStringLiteral("Alt+S"));
    hotkeys.insert(QStringLiteral("selection_translate"), QStringLiteral("Alt+X"));
    hotkeys.insert(QStringLiteral("toggle_main"), QStringLiteral("Alt+M"));
    hotkeys.insert(QStringLiteral("speak_clipboard"), QStringLiteral("Alt+R"));
    hotkeys.insert(QStringLiteral("text_replace"), QStringLiteral("Alt+T"));
    root.insert(QStringLiteral("hotkeys"), hotkeys);

    // Providers: priority order (mock is the hidden terminal fallback and
    // never appears here) + per-provider config blocks.
    // 2026-08：火山 crx 端点全语言 Bad Request、lingva.ml 被 Cloudflare 拦截，
    // 已移除；Bing 免费 token 端点（edge.microsoft.com/translate/auth）被微软
    // 下线（404），改用 Bing 网页版 ttranslatev3（国内直连免密钥）置顶。
    // google 保留代码但默认禁用：需翻墙，未挂代理时每次 3s 超时拖慢链路。
    QJsonObject providers;
    providers.insert(QStringLiteral("order"), QJsonArray{
        QStringLiteral("bing"), QStringLiteral("google"),
        QStringLiteral("mymemory"),
        QStringLiteral("deepl"), QStringLiteral("baidu"),
        QStringLiteral("youdao"), QStringLiteral("tencent"),
        QStringLiteral("openai"), QStringLiteral("zhipu"),
        QStringLiteral("deeplx")});
    providers.insert(QStringLiteral("google"), providerDefault(false));
    providers.insert(QStringLiteral("mymemory"), providerDefault(true));
    providers.insert(QStringLiteral("bing"), providerDefault(true));

    QJsonObject deepl = providerDefault(false);
    deepl.insert(QStringLiteral("apiKey"), QString());
    providers.insert(QStringLiteral("deepl"), deepl);

    QJsonObject baidu = providerDefault(false);
    baidu.insert(QStringLiteral("appId"), QString());
    baidu.insert(QStringLiteral("apiKey"), QString());
    providers.insert(QStringLiteral("baidu"), baidu);

    QJsonObject youdao = providerDefault(false);
    youdao.insert(QStringLiteral("appKey"), QString());
    youdao.insert(QStringLiteral("appSecret"), QString());
    providers.insert(QStringLiteral("youdao"), youdao);

    QJsonObject tencent = providerDefault(false);
    tencent.insert(QStringLiteral("secretId"), QString());
    tencent.insert(QStringLiteral("secretKey"), QString());
    tencent.insert(QStringLiteral("region"), QStringLiteral("ap-guangzhou"));
    providers.insert(QStringLiteral("tencent"), tencent);

    QJsonObject openai = providerDefault(false);
    openai.insert(QStringLiteral("baseUrl"), QStringLiteral("https://api.openai.com/v1"));
    openai.insert(QStringLiteral("apiKey"), QString());
    openai.insert(QStringLiteral("model"), QStringLiteral("gpt-4o-mini"));
    providers.insert(QStringLiteral("openai"), openai);

    // 2026-08：智谱 GLM-4-Flash 永久免费 + 国内直连，LLM 质量兜底源。
    QJsonObject zhipu = providerDefault(false);
    zhipu.insert(QStringLiteral("baseUrl"),
                 QStringLiteral("https://open.bigmodel.cn/api/paas/v4"));
    zhipu.insert(QStringLiteral("apiKey"), QString());
    zhipu.insert(QStringLiteral("model"), QStringLiteral("glm-4-flash"));
    providers.insert(QStringLiteral("zhipu"), zhipu);

    QJsonObject deeplx = providerDefault(false);
    deeplx.insert(QStringLiteral("baseUrl"), QString());
    providers.insert(QStringLiteral("deeplx"), deeplx);

    root.insert(QStringLiteral("providers"), providers);

    // Proxy: none / system / manual (HTTP proxy with optional auth).
    // 默认 system：Phase 1-3 时代依赖 Windows 系统代理（WinINET）可达 Google，
    // Phase 4 引入应用内代理后若默认 none 会绕过系统代理导致海外源不可达。
    // user_touched：用户在设置页手动改过代理后置 true，迁移时据此尊重用户选择。
    QJsonObject proxy;
    proxy.insert(QStringLiteral("mode"), QStringLiteral("system"));
    proxy.insert(QStringLiteral("host"), QString());
    proxy.insert(QStringLiteral("port"), 0);
    proxy.insert(QStringLiteral("user"), QString());
    proxy.insert(QStringLiteral("pass"), QString());
    proxy.insert(QStringLiteral("user_touched"), false);
    root.insert(QStringLiteral("proxy"), proxy);

    QJsonObject ocr;
    ocr.insert(QStringLiteral("engine"), QStringLiteral("paddle")); // "system" reserved (phase 5)
    root.insert(QStringLiteral("ocr"), ocr);

    QJsonObject tts;
    tts.insert(QStringLiteral("enabled"), true);
    // v0.7.2：双引擎。cloud=Edge 免费神经嗓音（默认，全语种），
    // system=QTextToSpeech（SAPI/WinRT）。云端嗓音偏好键 tts.cloud_voice.<lang>
    // 运行时按需写入，与系统引擎的 tts.voice.<lang> 平行互不干扰。
    tts.insert(QStringLiteral("engine"), QStringLiteral("cloud"));
    // v0.7.1 BUG-A：嗓音键改为按语言族动态 tts.voice.<lang>（运行时按需
    // 写入，无需预置）；旧键 voice_zh/voice_en 保留作 legacy 回退。
    tts.insert(QStringLiteral("voice_zh"), QString()); // empty = auto pick
    tts.insert(QStringLiteral("voice_en"), QString());
    root.insert(QStringLiteral("tts"), tts);

    // v0.7.1：目标语言单一真源（主窗下拉 ↔ 托盘子菜单双向同步）。
    // 纯字段新增：老配置缺 top-level "translate" 时 merge 自动补默认，
    // 无需 version 迁移。
    QJsonObject translate;
    translate.insert(QStringLiteral("target_lang"), QStringLiteral("zh-CN"));
    root.insert(QStringLiteral("translate"), translate);

    QJsonObject selection;
    selection.insert(QStringLiteral("enabled"), true);
    root.insert(QStringLiteral("selection"), selection);

    // 通知开关（phase 5）：总开关 + 场景类目，默认全开。
    // translate_failed（phase 6）：翻译链路落到 mock 兜底时提示用户服务不可达。
    // hotkey_conflict（v0.7.2）：热键被占用的气泡提示，默认关（只写
    // qWarning 日志，解决 Alt+R 被占每次启动弹气泡骚扰）。
    QJsonObject notifications;
    notifications.insert(QStringLiteral("enabled"), true);
    notifications.insert(QStringLiteral("capture_ocr"), true);
    notifications.insert(QStringLiteral("capture_translate"), true);
    notifications.insert(QStringLiteral("selection"), true);
    notifications.insert(QStringLiteral("replace"), true);
    notifications.insert(QStringLiteral("translate_failed"), true);
    notifications.insert(QStringLiteral("hotkey_conflict"), false);
    root.insert(QStringLiteral("notifications"), notifications);

    // v0.7.2：托盘提示状态。首次最小化到托盘弹一次气泡后置 true，
    // 之后永不再弹（跨进程持久，不再是每次启动都提示）。
    QJsonObject tray;
    tray.insert(QStringLiteral("minimized_hint_shown"), false);
    root.insert(QStringLiteral("tray"), tray);

    QJsonObject history;
    history.insert(QStringLiteral("limit"), 5000);
    root.insert(QStringLiteral("history"), history);

    return root;
}

void ConfigManager::load()
{
    QFile file(m_filePath);
    if (!file.exists()) {
        m_root = defaults();
        save(); // create the initial file (also creates the directory)
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        m_root = defaults();
        return;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        // Corrupt: keep the evidence as config.json.bak, fall back to defaults.
        const QString bak = m_filePath + QStringLiteral(".bak");
        QFile::remove(bak);
        QFile::copy(m_filePath, bak);
        m_recoveredFromCorrupt = true;
        m_root = defaults();
        save();
        return;
    }

    // Merge over defaults so config files from older versions gain new keys.
    QJsonObject merged = defaults();
    QJsonObject loaded = doc.object();

    // 版本驱动的迁移：在 top-level merge 之前就地改写 loaded，否则 proxy
    // 子对象会被老配置整体覆盖回旧默认值，新增的 user_touched 字段补不进来。
    {
        const int ver = loaded.value(QStringLiteral("version")).toInt(1);
        if (ver < 2) {
            QJsonObject proxy = loaded.value(QStringLiteral("proxy")).toObject();
            const bool touched = proxy.value(QStringLiteral("user_touched")).toBool(false);
            // 仅当用户从未在设置页动过代理时迁移：尊重用户显式选择。
            if (!touched && proxy.value(QStringLiteral("mode")).toString()
                                == QLatin1String("none")) {
                proxy.insert(QStringLiteral("mode"), QStringLiteral("system"));
                qInfo().noquote() << QStringLiteral(
                    "[config] v1->v2 migrate proxy.mode none->system "
                    "(user_touched=false, restoring Phase 1-3 system-proxy behavior)");
            }
            proxy.insert(QStringLiteral("user_touched"), touched);
            loaded.insert(QStringLiteral("proxy"), proxy);
            loaded.insert(QStringLiteral("version"), 2);
        }
        // v2→v3：纯字段新增 ui.reduce_transparency（默认 false）。
        // 无语义改写，仅对 loaded.ui 就地补字段，避免 merge 阶段 loaded.ui
        // 整体覆盖 defaults.ui 导致字段丢失。
        // 注意：不写回 version=3！runConfig selftest 断言 v1 配置加载后
        // version==2（契约保护）。version 字段由 defaults(=3) 与 loaded
        // merge 自然决定：新装=3，老 v1/v2 配置升上来=2，已是 v3 的=3。
        if (ver < 3) {
            QJsonObject ui = loaded.value(QStringLiteral("ui")).toObject();
            if (!ui.contains(QStringLiteral("reduce_transparency"))) {
                ui.insert(QStringLiteral("reduce_transparency"), false);
                loaded.insert(QStringLiteral("ui"), ui);
                qInfo().noquote() << QStringLiteral(
                    "[config] v2->v3 migrate: ui.reduce_transparency added (default false)");
            }
        }
        // v3→v4（Phase 7-fix1）：纯字段新增 ui.font 子对象。
        // 无语义改写，仅对 loaded.ui 就地补字段。同样不写回 version=4：
        // runConfig selftest 断言 v1 配置加载后 version==2（契约保护）。
        if (ver < 4) {
            QJsonObject ui = loaded.value(QStringLiteral("ui")).toObject();
            QJsonObject font = ui.value(QStringLiteral("font")).toObject();
            if (!font.contains(QStringLiteral("source_pt")))
                font.insert(QStringLiteral("source_pt"), 11);
            if (!font.contains(QStringLiteral("result_pt")))
                font.insert(QStringLiteral("result_pt"), 11);
            if (!font.contains(QStringLiteral("result_color")))
                font.insert(QStringLiteral("result_color"), QStringLiteral("theme"));
            if (!font.contains(QStringLiteral("result_color_custom")))
                font.insert(QStringLiteral("result_color_custom"),
                            QStringLiteral("#1F2937"));
            ui.insert(QStringLiteral("font"), font);
            loaded.insert(QStringLiteral("ui"), ui);
            qInfo().noquote() << QStringLiteral(
                "[config] v3->v4 migrate: ui.font added (default 11pt/theme)");
        }
        // 2026-08 免密钥源洗牌：老配置的 providers 子对象会在 merge 阶段
        // 整体覆盖 defaults，新默认值补不进来，故在 loaded 上就地改写：
        // - order 移除已死的 volcano（火山 crx 端点全语言 Bad Request）与
        //   lingva（lingva.ml 被 Cloudflare 人机验证拦截）；
        // - bing 提到最前并启用（Bing 网页版 ttranslatev3，国内直连免密钥）；
        // - google 默认禁用（需翻墙，未挂代理时每次 3s 超时拖慢链路）。
        // 不写回 version：config_atomic_test 的 version 契约保持不变。
        {
            QJsonObject prov = loaded.value(QStringLiteral("providers")).toObject();
            QJsonArray order = prov.value(QStringLiteral("order")).toArray();
            QStringList orderList;
            for (const QJsonValue &v : order)
                orderList.append(v.toString());

            bool changed = false;
            changed |= (orderList.removeAll(QStringLiteral("volcano")) > 0);
            changed |= (orderList.removeAll(QStringLiteral("lingva")) > 0);

            // bing 强制提到最前（无论是否已在列表）。
            orderList.removeAll(QStringLiteral("bing"));
            orderList.prepend(QStringLiteral("bing"));
            changed = true;

            QJsonObject bing = prov.value(QStringLiteral("bing")).toObject();
            if (!bing.value(QStringLiteral("enabled")).toBool(true)) {
                bing.insert(QStringLiteral("enabled"), true);
                prov.insert(QStringLiteral("bing"), bing);
                changed = true;
            }

            QJsonObject google = prov.value(QStringLiteral("google")).toObject();
            if (google.value(QStringLiteral("enabled")).toBool(false)) {
                google.insert(QStringLiteral("enabled"), false);
                prov.insert(QStringLiteral("google"), google);
                changed = true;
            }

            // 智谱 GLM-4-Flash（永久免费 + 国内直连）：老配置缺块时补默认，
            // order 追加到 openai 之后。
            if (!prov.contains(QStringLiteral("zhipu"))) {
                QJsonObject zhipu;
                zhipu.insert(QStringLiteral("enabled"), false);
                zhipu.insert(QStringLiteral("baseUrl"),
                             QStringLiteral("https://open.bigmodel.cn/api/paas/v4"));
                zhipu.insert(QStringLiteral("apiKey"), QString());
                zhipu.insert(QStringLiteral("model"), QStringLiteral("glm-4-flash"));
                prov.insert(QStringLiteral("zhipu"), zhipu);
                changed = true;
            }
            if (!orderList.contains(QStringLiteral("zhipu"))) {
                const int idx = orderList.indexOf(QStringLiteral("openai"));
                orderList.insert(idx >= 0 ? idx + 1 : orderList.size(),
                                 QStringLiteral("zhipu"));
                changed = true;
            }

            // 死源配置块就地清理，避免设置页残留无效条目。
            if (prov.contains(QStringLiteral("volcano"))) {
                prov.remove(QStringLiteral("volcano"));
                changed = true;
            }
            if (prov.contains(QStringLiteral("lingva"))) {
                prov.remove(QStringLiteral("lingva"));
                changed = true;
            }

            if (changed) {
                QJsonArray newOrder;
                for (const QString &n : orderList)
                    newOrder.append(n);
                prov.insert(QStringLiteral("order"), newOrder);
                loaded.insert(QStringLiteral("providers"), prov);
                qInfo().noquote() << QStringLiteral(
                    "[config] providers migrate: bing to front & enabled, "
                    "volcano/lingva dropped, google disabled (needs proxy)");
            }
        }
    }

    for (auto it = loaded.constBegin(); it != loaded.constEnd(); ++it)
        merged.insert(it.key(), it.value());
    m_root = merged;
}

bool ConfigManager::save() const
{
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    QSaveFile file(m_filePath); // temp file + atomic rename on commit
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(m_root).toJson(QJsonDocument::Indented));
    return file.commit();
}

QJsonValue ConfigManager::value(const QString &path) const
{
    return lookup(m_root, path);
}

void ConfigManager::setValue(const QString &path, const QJsonValue &val)
{
    const QStringList parts = path.split(QLatin1Char('.'));
    if (parts.isEmpty())
        return;
    m_root = withValue(m_root, parts, 0, val);
    save();
    emit configChanged(path);
}

QString ConfigManager::hotkeyFor(const QString &actionId) const
{
    return value(QStringLiteral("hotkeys.") + actionId).toString();
}

void ConfigManager::setHotkey(const QString &actionId, const QString &keySeq)
{
    setValue(QStringLiteral("hotkeys.") + actionId, keySeq);
}

QStringList ConfigManager::providerOrder() const
{
    QStringList order;
    const QJsonArray arr = value(QStringLiteral("providers.order")).toArray();
    for (const QJsonValue &v : arr)
        order.append(v.toString());
    return order;
}

void ConfigManager::setProviderOrder(const QStringList &order)
{
    QJsonArray arr;
    for (const QString &name : order)
        arr.append(name);
    setValue(QStringLiteral("providers.order"), arr);
}

QJsonObject ConfigManager::providerConfig(const QString &name) const
{
    return value(QStringLiteral("providers.") + name).toObject();
}

void ConfigManager::setProviderField(const QString &name, const QString &field,
                                     const QJsonValue &val)
{
    setValue(QStringLiteral("providers.%1.%2").arg(name, field), val);
}

bool ConfigManager::notificationsEnabled() const
{
    return boolValue(QStringLiteral("notifications.enabled"));
}

bool ConfigManager::notificationCategoryEnabled(const QString &category) const
{
    // 总开关关 -> 全关；总开关开时再看逐项（缺省视为开）。
    if (!notificationsEnabled())
        return false;
    const QJsonValue v = value(QStringLiteral("notifications.") + category);
    // v0.7.2 hotkey_conflict 缺省 = false（默认不弹，仅日志）：老配置
    // notifications 子对象整体覆盖 defaults 后缺该键，若沿用"缺省视为开"
    // 会让升级用户继续被气泡骚扰，故该类目单独缺省为关。
    if (category == QLatin1String("hotkey_conflict"))
        return v.isBool() ? v.toBool() : false;
    // 缺省 = true（保持默认全开语义，老配置文件升级时也能正确显示通知）。
    return v.isBool() ? v.toBool() : true;
}
