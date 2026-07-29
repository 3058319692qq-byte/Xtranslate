#include "core/tts/EdgeTtsProvider.h"

#include "core/config/ConfigManager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QLocale>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QWebSocket>

namespace {

// Edge "大声朗读"公开固定 token（来源：Edge 浏览器内置 Read Aloud 功能，
// 开源 edge-tts 项目同款常量）。非用户密钥、非机密，硬编码合规。
constexpr char kTrustedClientToken[] = "6A5AA1D4EAFF4E9FB37E23D68491D6F4";

// 服务端要求携带 Edge 浏览器 UA / Origin，否则握手被拒（403）。
// 版本号与 Sec-MS-GEC-Version 保持同源（参考 edge-tts 当前常量 143，
// 旧版本号会被服务端拒绝）。
constexpr char kEdgeUserAgent[] =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36 Edg/143.0.0.0";
constexpr char kEdgeOrigin[] =
    "chrome-extension://jdiccldimpdaibmpdkjnbmckianbfold";
constexpr char kSecMsGecVersion[] = "1-143.0.3650.75";

// 整体超时：连接 + 合成 + 收流。超过即失败让上层回退系统嗓音。
constexpr int kTimeoutMs = 10000;

// lang -> Edge 神经嗓音候选表。首项为默认；覆盖主窗目标语言全集
// （LangCatalog 除 auto 外全部语言）。
struct EdgeVoiceEntry {
    const char *lang;      // BCP-47（与 LangCatalog code 同源）
    const char *voices[4]; // 候选嗓音，nullptr 结尾
};
const EdgeVoiceEntry kEdgeVoices[] = {
    {"zh-CN", {"zh-CN-XiaoxiaoNeural", "zh-CN-YunxiNeural",
               "zh-CN-YunyangNeural", nullptr}},
    {"zh-TW", {"zh-TW-HsiaoChenNeural", "zh-TW-YunJheNeural",
               "zh-TW-HsiaoYuNeural", nullptr}},
    {"en",    {"en-US-AriaNeural", "en-US-GuyNeural",
               "en-GB-SoniaNeural", nullptr}},
    {"ja",    {"ja-JP-NanamiNeural", "ja-JP-KeitaNeural", nullptr, nullptr}},
    {"ko",    {"ko-KR-SunHiNeural", "ko-KR-InJoonNeural", nullptr, nullptr}},
    {"fr",    {"fr-FR-DeniseNeural", "fr-FR-HenriNeural", nullptr, nullptr}},
    {"de",    {"de-DE-KatjaNeural", "de-DE-ConradNeural", nullptr, nullptr}},
    {"es",    {"es-ES-ElviraNeural", "es-ES-AlvaroNeural", nullptr, nullptr}},
    {"ru",    {"ru-RU-SvetlanaNeural", "ru-RU-DmitryNeural", nullptr, nullptr}},
};

// 语言归一化：完整 code 精确命中优先，其次语言族前缀（"zh-CN"→"zh-CN"，
// "en-US"→"en"）。返回表项指针，缺失返回 nullptr。
const EdgeVoiceEntry *entryFor(const QString &langBcp47)
{
    for (const EdgeVoiceEntry &e : kEdgeVoices) {
        if (langBcp47.compare(QLatin1String(e.lang), Qt::CaseInsensitive) == 0)
            return &e;
    }
    const QString family =
        QLocale::languageToCode(QLocale(langBcp47).language());
    for (const EdgeVoiceEntry &e : kEdgeVoices) {
        if (QString::fromLatin1(e.lang)
                .startsWith(family, Qt::CaseInsensitive))
            return &e;
    }
    return nullptr;
}

// Sec-MS-GEC 防滥用参数（2024 起服务端要求）：Windows FILETIME 按 5 分钟
// 取整后拼 TrustedClientToken 求 SHA-256（大写 hex）。算法为公开逆向结论
// （edge-tts 同款），不含任何用户机密。
QString secMsGec()
{
    qint64 ticks = QDateTime::currentSecsSinceEpoch() + 11644473600LL;
    ticks -= ticks % 300;               // 5 分钟窗口
    ticks *= 10000000LL;                // 秒 -> 100ns
    const QByteArray input =
        QByteArray::number(ticks) + kTrustedClientToken;
    return QString::fromLatin1(
        QCryptographicHash::hash(input, QCryptographicHash::Sha256)
            .toHex().toUpper());
}

// SSML 文本转义（& < > ' " 五个实体）。
QString xmlEscape(QString text)
{
    text.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    text.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    text.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    text.replace(QLatin1Char('\''), QLatin1String("&apos;"));
    text.replace(QLatin1Char('"'), QLatin1String("&quot;"));
    return text;
}

} // namespace

EdgeTtsProvider::EdgeTtsProvider(QObject *parent)
    : QObject(parent)
{
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(kTimeoutMs);
    connect(m_timeout, &QTimer::timeout, this, [this]() {
        abortCurrent(QStringLiteral("timeout after %1 ms").arg(kTimeoutMs));
    });
}

EdgeTtsProvider::~EdgeTtsProvider()
{
    // 析构时不再回调（进程收尾阶段上层可能已析构）。
    m_finished = nullptr;
    if (m_socket)
        m_socket->abort();
}

QString EdgeTtsProvider::displayName() const
{
    return QStringLiteral("Edge TTS");
}

bool EdgeTtsProvider::isReachable() const
{
    // 不做同步探测（会阻塞 GUI 线程）：真实可达性由 synthesize 的
    // 超时/错误路径判定，失败即由上层回退。
    return !qEnvironmentVariableIsSet("XT_TTS_FORCE_CLOUD_FAIL");
}

QString EdgeTtsProvider::voiceConfigKey(const QString &langBcp47)
{
    return QStringLiteral("tts.cloud_voice.")
        + QLocale::languageToCode(QLocale(langBcp47).language());
}

QString EdgeTtsProvider::voiceForLang(const QString &langBcp47)
{
    // 用户在设置页选过的云端嗓音优先。
    const QString preferred =
        ConfigManager::instance().stringValue(voiceConfigKey(langBcp47));
    if (!preferred.isEmpty())
        return preferred;
    if (const EdgeVoiceEntry *e = entryFor(langBcp47))
        return QString::fromLatin1(e->voices[0]);
    // 映射缺失（未来新增语言）：Aria 多语言兜底，保证仍有声。
    return QStringLiteral("en-US-AriaNeural");
}

QStringList EdgeTtsProvider::voicesForLang(const QString &langBcp47)
{
    QStringList names;
    if (const EdgeVoiceEntry *e = entryFor(langBcp47)) {
        for (const char *const *v = e->voices; *v; ++v)
            names.append(QString::fromLatin1(*v));
    }
    return names;
}

int EdgeTtsProvider::voiceMapSize()
{
    return static_cast<int>(sizeof(kEdgeVoices) / sizeof(kEdgeVoices[0]));
}

void EdgeTtsProvider::abortCurrent(const QString &reason)
{
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_timeout->stop();
    finishCurrent(QByteArray(), reason);
}

void EdgeTtsProvider::finishCurrent(const QByteArray &audio,
                                    const QString &error)
{
    if (!m_finished)
        return;
    // 先置空再回调：finished 内部可能立刻发起下一次 synthesize。
    auto cb = std::move(m_finished);
    m_finished = nullptr;
    cb(audio, error);
}

void EdgeTtsProvider::synthesize(
    const QString &text, const QString &langBcp47,
    std::function<void(QByteArray audio, QString error)> finished)
{
    // 上一个请求还在飞：中止并如实告知 cancelled。
    if (m_finished)
        abortCurrent(QStringLiteral("cancelled by newer request"));
    m_finished = std::move(finished);
    m_audio.clear();

    // selftest 钩子：确定性模拟"云端不可达"，验证上层回退链。
    if (qEnvironmentVariableIsSet("XT_TTS_FORCE_CLOUD_FAIL")) {
        QTimer::singleShot(0, this, [this]() {
            finishCurrent(QByteArray(),
                          QStringLiteral("forced failure "
                                         "(XT_TTS_FORCE_CLOUD_FAIL)"));
        });
        return;
    }

    const QString voice = voiceForLang(langBcp47);
    const QString connectionId = QUuid::createUuid().toString(QUuid::Id128);

    QUrl url(QStringLiteral(
        "wss://speech.platform.bing.com/consumer/speech/synthesize/"
        "readaloud/edge/v1"));
    // Sec-MS-GEC 走 query（服务端 2024 起校验；见文件头合规说明）。
    url.setQuery(QStringLiteral("TrustedClientToken=%1&Sec-MS-GEC=%2"
                                "&Sec-MS-GEC-Version=%3&ConnectionId=%4")
                     .arg(QLatin1String(kTrustedClientToken), secMsGec(),
                          QLatin1String(kSecMsGecVersion), connectionId));

    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", kEdgeUserAgent);
    req.setRawHeader("Origin", kEdgeOrigin);
    req.setRawHeader("Pragma", "no-cache");
    req.setRawHeader("Cache-Control", "no-cache");

    m_socket = new QWebSocket(QString(),
                              QWebSocketProtocol::VersionLatest, this);

    connect(m_socket, &QWebSocket::connected, this,
            [this, voice, langBcp47, text]() {
        const QString ts = QDateTime::currentDateTimeUtc()
                               .toString(Qt::ISODate);
        // 1) speech.config：声明输出格式 24khz/48kbit mono mp3。
        m_socket->sendTextMessage(QStringLiteral(
            "X-Timestamp:%1\r\n"
            "Content-Type:application/json; charset=utf-8\r\n"
            "Path:speech.config\r\n\r\n"
            "{\"context\":{\"synthesis\":{\"audio\":{\"metadataoptions\":"
            "{\"sentenceBoundaryEnabled\":\"false\","
            "\"wordBoundaryEnabled\":\"false\"},"
            "\"outputFormat\":\"audio-24khz-48kbitrate-mono-mp3\"}}}}")
                .arg(ts));
        // 2) SSML：voice + 转义正文。
        m_socket->sendTextMessage(QStringLiteral(
            "X-RequestId:%1\r\n"
            "Content-Type:application/ssml+xml\r\n"
            "X-Timestamp:%2\r\n"
            "Path:ssml\r\n\r\n"
            "<speak version='1.0' "
            "xmlns='http://www.w3.org/2001/10/synthesis' xml:lang='%3'>"
            "<voice name='%4'>%5</voice></speak>")
                .arg(QUuid::createUuid().toString(QUuid::Id128), ts,
                     langBcp47, voice, xmlEscape(text)));
    });

    // 二进制帧：前 2 字节大端 = 头长度；头含 Path:audio 时其后即 mp3 分片。
    connect(m_socket, &QWebSocket::binaryMessageReceived, this,
            [this](const QByteArray &frame) {
        if (frame.size() < 2)
            return;
        const int headerLen =
            (static_cast<quint8>(frame[0]) << 8)
            | static_cast<quint8>(frame[1]);
        if (frame.size() < 2 + headerLen)
            return;
        const QByteArray header = frame.mid(2, headerLen);
        if (header.contains("Path:audio"))
            m_audio.append(frame.mid(2 + headerLen));
    });

    // 文本帧：turn.end = 本次合成完毕，音频已收全。
    connect(m_socket, &QWebSocket::textMessageReceived, this,
            [this](const QString &msg) {
        if (!msg.contains(QLatin1String("Path:turn.end")))
            return;
        m_timeout->stop();
        m_socket->disconnect(this);
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
        if (m_audio.isEmpty())
            finishCurrent(QByteArray(),
                          QStringLiteral("turn.end with empty audio"));
        else
            finishCurrent(m_audio, QString());
    });

    connect(m_socket, &QWebSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
        const QString err = m_socket ? m_socket->errorString()
                                     : QStringLiteral("socket error");
        abortCurrent(QStringLiteral("websocket error: %1").arg(err));
    });

    // 服务端异常断开（未发 turn.end）也按失败处理。
    connect(m_socket, &QWebSocket::disconnected, this, [this]() {
        if (m_finished)
            abortCurrent(QStringLiteral("connection closed prematurely"));
    });

    m_timeout->start();
    m_socket->open(req);
}
