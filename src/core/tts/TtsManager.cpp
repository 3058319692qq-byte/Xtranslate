#include "core/tts/TtsManager.h"

#include "core/config/ConfigManager.h"
#include "core/tts/EdgeTtsProvider.h"

#include <QAudioOutput>
#include <QBuffer>
#include <QDebug>
#include <QLocale>
#include <QMediaPlayer>
#include <QTextToSpeech>
#include <QUrl>
#include <QVoice>

namespace {

// v0.7.1 BUG-A：用户偏好嗓音配置键改为按语言族动态 tts.voice.<lang>
// （如 tts.voice.ja / tts.voice.ko / tts.voice.fr），不再写死 zh/en 两个。
// 旧键 tts.voice_zh / tts.voice_en 作为 legacy 回退保留，老配置不丢。
QString voiceCfgKeyFor(const QLocale &want)
{
    return QStringLiteral("tts.voice.")
        + QLocale::languageToCode(want.language());
}

// User-preferred voice from config (tts.voice.<lang>, legacy voice_zh/_en) ->
// exact locale match -> language-only match -> invalid voice (engine default).
QVoice pickVoice(QTextToSpeech *tts, const QString &langBcp47)
{
    const QLocale want(langBcp47);

    QString preferred =
        ConfigManager::instance().stringValue(voiceCfgKeyFor(want));
    if (preferred.isEmpty()) {
        // legacy 回退：v0.7.0 前只有 voice_zh/voice_en 两个固定键。
        if (want.language() == QLocale::Chinese)
            preferred = ConfigManager::instance().stringValue(
                QStringLiteral("tts.voice_zh"));
        else if (want.language() == QLocale::English)
            preferred = ConfigManager::instance().stringValue(
                QStringLiteral("tts.voice_en"));
    }
    if (!preferred.isEmpty()) {
        const QList<QVoice> all = tts->findVoices();
        for (const QVoice &v : all) {
            if (v.name() == preferred)
                return v;
        }
    }

    const QList<QVoice> exact = tts->findVoices(want);
    if (!exact.isEmpty())
        return exact.first();

    const QList<QVoice> byLanguage = tts->findVoices(want.language());
    if (!byLanguage.isEmpty())
        return byLanguage.first();

    return QVoice();
}

} // namespace

TtsManager &TtsManager::instance()
{
    static TtsManager mgr;
    return mgr;
}

TtsManager::TtsManager()
{
    // Default platform engine (SAPI/WinRT on Windows). Construction is cheap;
    // voice enumeration below decides availability.
    m_tts = new QTextToSpeech(this);

    if (m_tts->state() != QTextToSpeech::Error)
        m_voiceCount = static_cast<int>(m_tts->findVoices().size());
    m_available = m_voiceCount > 0;
    // v0.7.1 BUG-A：记住引擎默认嗓音。某语言无专用语音包时 speak() 显式
    // 回到它继续朗读（而非沿用上一次 setVoice 残留的其它语种嗓音）。
    if (m_available)
        m_defaultVoice = m_tts->voice();
}

QString TtsManager::voiceConfigKey(const QString &langBcp47)
{
    return voiceCfgKeyFor(QLocale(langBcp47));
}

QString TtsManager::engine()
{
    // v0.7.2：默认云端（Edge 免费神经嗓音全语种，解决韩语等本机无
    // 语音包无声问题）；老配置缺 tts.engine 键同样落到 cloud。
    const QString v = ConfigManager::instance().stringValue(
        QStringLiteral("tts.engine"));
    return v == QLatin1String("system") ? QStringLiteral("system")
                                         : QStringLiteral("cloud");
}

QString TtsManager::voiceNameFor(const QString &langBcp47) const
{
    if (!m_available)
        return QString();
    const QVoice voice = pickVoice(m_tts, langBcp47);
    return voice.name();
}

QStringList TtsManager::voiceNamesFor(const QString &langBcp47) const
{
    QStringList names;
    if (!m_available)
        return names;
    const QLocale want(langBcp47);
    const QList<QVoice> voices = m_tts->findVoices(want.language());
    for (const QVoice &v : voices)
        names.append(v.name());
    return names;
}

void TtsManager::speak(const QString &text, const QString &langBcp47)
{
    // Config master switch (settings TTS page); silently ignore when off.
    if (!ConfigManager::instance().ttsEnabled())
        return;
    if (text.trimmed().isEmpty())
        return;
    speakWith(engine(), text, langBcp47);
}

void TtsManager::speakWith(const QString &engine, const QString &text,
                           const QString &langBcp47)
{
    if (text.trimmed().isEmpty())
        return;

    stop();
    const quint64 seq = ++m_speakSeq;

    if (engine == QLatin1String("cloud")) {
        if (!m_edge)
            m_edge = new EdgeTtsProvider(this);
        m_edge->synthesize(text, langBcp47,
                           [this, seq, text, langBcp47](QByteArray audio,
                                                        QString error) {
            // 序号门控：stop()/新 speak 之后的迟到回调直接丢弃。
            if (seq != m_speakSeq)
                return;
            if (error.isEmpty() && !audio.isEmpty()) {
                qInfo().noquote() << QStringLiteral(
                    "[tts] cloud ok lang=%1 voice=%2 bytes=%3")
                    .arg(langBcp47,
                         EdgeTtsProvider::voiceForLang(langBcp47))
                    .arg(audio.size());
                playCloudAudio(audio);
                emit utteranceStarted(QStringLiteral("cloud"));
            } else {
                // 回退链第一站：云端失败 → 系统嗓音（日志可查，不弹窗不崩）。
                qWarning().noquote() << QStringLiteral(
                    "[tts] cloud failed (%1), falling back to system voice "
                    "lang=%2").arg(error, langBcp47);
                speakSystem(text, langBcp47);
                emit utteranceStarted(QStringLiteral("system_fallback"));
            }
        });
        return;
    }

    speakSystem(text, langBcp47);
    emit utteranceStarted(QStringLiteral("system"));
}

void TtsManager::speakSystem(const QString &text, const QString &langBcp47)
{
    if (!m_available) {
        if (!m_unavailableNotified) {
            m_unavailableNotified = true;
            emit unavailable(QStringLiteral("本机没有可用的语音引擎，朗读功能不可用"));
        }
        return;
    }

    m_tts->stop();
    const QVoice voice = pickVoice(m_tts, langBcp47);
    if (!voice.name().isEmpty()) {
        m_tts->setVoice(voice);
    } else if (!m_defaultVoice.name().isEmpty()) {
        // 本机缺该语言语音包：兜底到引擎默认嗓音仍朗读（不静默失败）。
        m_tts->setVoice(m_defaultVoice);
    }
    m_tts->say(text);
}

void TtsManager::playCloudAudio(const QByteArray &mp3)
{
    // 懒创建播放链：Qt Multimedia FFmpeg 后端解 mp3，QBuffer 作数据源。
    if (!m_player) {
        m_player = new QMediaPlayer(this);
        m_audioOut = new QAudioOutput(this);
        m_player->setAudioOutput(m_audioOut);
        m_audioBuf = new QBuffer(this);
    }
    m_player->stop();
    m_player->setSourceDevice(nullptr); // 解绑旧 buffer，允许重新 setData
    if (m_audioBuf->isOpen())
        m_audioBuf->close();
    m_audioData = mp3;
    m_audioBuf->setData(m_audioData);
    m_audioBuf->open(QIODevice::ReadOnly);
    // 给后端一个 .mp3 后缀提示，免得探流失败。
    m_player->setSourceDevice(m_audioBuf,
                              QUrl(QStringLiteral("edge-tts.mp3")));
    m_player->play();
}

void TtsManager::stop()
{
    // 作废飞行中的云端合成回调 + 停止两个播放后端。
    ++m_speakSeq;
    if (m_player)
        m_player->stop();
    if (m_available)
        m_tts->stop();
}

QString TtsManager::guessLang(const QString &text)
{
    // v0.7.1 BUG-A：细分日/韩/中。假名是日文独有→ja 优先；谚文→ko；
    // 仅含 CJK 汉字→zh-CN；其余→en。只服务于朗读剪贴板等未显式带 lang
    // 的场景；主窗/划词朗读应传当前目标语言（dstLang）而非走此启发式。
    bool hasHan = false;
    bool hasHangul = false;
    for (QChar ch : text) {
        const ushort u = ch.unicode();
        if (u >= 0x3040 && u <= 0x30FF)
            return QStringLiteral("ja");            // 平/片假名
        if (u >= 0xAC00 && u <= 0xD7AF)
            hasHangul = true;                         // 谚文音节
        else if ((u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3400 && u <= 0x4DBF))
            hasHan = true;                            // CJK 汉字
    }
    if (hasHangul)
        return QStringLiteral("ko");
    if (hasHan)
        return QStringLiteral("zh-CN");
    return QStringLiteral("en");
}
