// TtsManager - text-to-speech wrapper around QTextToSpeech (phase 3).
//
// Singleton on the GUI thread. speak(text, langBcp47) picks the best QVoice
// for the requested language (exact locale match -> language match -> engine
// default) and queues the utterance; a new speak() interrupts the previous
// one. When the platform engine has no voices at all, isAvailable() is false,
// all 朗读 buttons should be disabled and `unavailable` fires exactly once so
// the tray can show a single bubble.

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QTextToSpeech;

class TtsManager : public QObject
{
    Q_OBJECT
public:
    static TtsManager &instance();

    bool isAvailable() const { return m_available; }
    int voiceCount() const { return m_voiceCount; }

    // Name of the voice speak() would use for `langBcp47` ("zh-CN", "en",...);
    // empty when no voice of that language exists (engine default is used).
    QString voiceNameFor(const QString &langBcp47) const;

    // All voice names for a language (settings drop-downs, phase 4).
    QStringList voiceNamesFor(const QString &langBcp47) const;

    // Interrupts any current utterance. No-op (plus one-shot `unavailable`
    // signal) when the engine has no voices. Also a silent no-op while the
    // config switch tts.enabled is off.
    void speak(const QString &text, const QString &langBcp47);
    void stop();

    // Simple heuristic shared by 朗读剪贴板 and the popup card: any CJK
    // character -> "zh-CN", otherwise "en".
    static QString guessLang(const QString &text);

signals:
    // Emitted at most once, on the first speak() attempt without an engine.
    void unavailable(const QString &message);

private:
    TtsManager();

    QTextToSpeech *m_tts = nullptr;
    bool m_available = false;
    int m_voiceCount = 0;
    bool m_unavailableNotified = false;
};
