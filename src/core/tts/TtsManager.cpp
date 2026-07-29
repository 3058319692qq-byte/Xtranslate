#include "core/tts/TtsManager.h"

#include "core/config/ConfigManager.h"

#include <QLocale>
#include <QTextToSpeech>
#include <QVoice>

namespace {

// User-preferred voice from config (tts.voice_zh / tts.voice_en) ->
// exact locale match -> language-only match -> invalid voice (engine default).
QVoice pickVoice(QTextToSpeech *tts, const QString &langBcp47)
{
    const QLocale want(langBcp47);

    const QString cfgKey = want.language() == QLocale::Chinese
        ? QStringLiteral("tts.voice_zh") : QStringLiteral("tts.voice_en");
    const QString preferred = ConfigManager::instance().stringValue(cfgKey);
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
    if (!m_available) {
        if (!m_unavailableNotified) {
            m_unavailableNotified = true;
            emit unavailable(QStringLiteral("本机没有可用的语音引擎，朗读功能不可用"));
        }
        return;
    }
    if (text.trimmed().isEmpty())
        return;

    m_tts->stop();
    const QVoice voice = pickVoice(m_tts, langBcp47);
    if (!voice.name().isEmpty())
        m_tts->setVoice(voice);
    m_tts->say(text);
}

void TtsManager::stop()
{
    if (m_available)
        m_tts->stop();
}

QString TtsManager::guessLang(const QString &text)
{
    for (QChar ch : text) {
        const ushort u = ch.unicode();
        // CJK ideographs, kana and hangul all route to the zh voice family
        // for now; refined per-language detection is a later-phase concern.
        if ((u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3400 && u <= 0x4DBF)
            || (u >= 0x3040 && u <= 0x30FF) || (u >= 0xAC00 && u <= 0xD7AF))
            return QStringLiteral("zh-CN");
    }
    return QStringLiteral("en");
}
