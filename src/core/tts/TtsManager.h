// TtsManager - text-to-speech wrapper around QTextToSpeech (phase 3).
//
// Singleton on the GUI thread. speak(text, langBcp47) picks the best QVoice
// for the requested language (exact locale match -> language match -> engine
// default) and queues the utterance; a new speak() interrupts the previous
// one. When the platform engine has no voices at all, isAvailable() is false,
// all 朗读 buttons should be disabled and `unavailable` fires exactly once so
// the tray can show a single bubble.
//
// v0.7.2 双引擎：配置 tts.engine = "cloud"(默认, Edge 免费神经嗓音) |
// "system"(QTextToSpeech)。cloud 路径：EdgeTtsProvider 合成 mp3 →
// QMediaPlayer(FFmpeg 后端) 播放；失败(断网/接口变更/超时)自动回退
// system（现有链：无该语言语音包再回退默认嗓音），回退过程 qWarning
// 可查，utteranceStarted 上报实际生效引擎（selftest 回退链取证）。

#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVoice>

class QTextToSpeech;
class QMediaPlayer;
class QAudioOutput;
class QBuffer;
class EdgeTtsProvider;

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

    // v0.7.1：用户偏好嗓音的配置键（按语言族，如 "tts.voice.ja"），
    // 设置页与 pickVoice 共用同一映射避免键名漂移。
    static QString voiceConfigKey(const QString &langBcp47);

    // v0.7.2：当前朗读引擎（配置 tts.engine，缺省/非法值→"cloud"）。
    static QString engine();

    // Interrupts any current utterance. No-op (plus one-shot `unavailable`
    // signal) when the engine has no voices. Also a silent no-op while the
    // config switch tts.enabled is off.
    void speak(const QString &text, const QString &langBcp47);

    // v0.7.2：指定引擎朗读（"cloud"|"system"），不读 tts.enabled 总开关
    // （speak() 检查后委派至此）。--selftest tts 用它确定性验证回退链。
    void speakWith(const QString &engine, const QString &text,
                   const QString &langBcp47);
    void stop();

    // Simple heuristic shared by 朗读剪贴板 and the popup card（v0.7.1：
    // 假名→"ja"，谚文→"ko"，CJK 汉字→"zh-CN"，其余→"en"）。仅限未显式
    // 带 lang 的场景；主窗/划词朗读传当前目标语言（dstLang）。
    static QString guessLang(const QString &text);

signals:
    // Emitted at most once, on the first speak() attempt without an engine.
    void unavailable(const QString &message);

    // v0.7.2：一次朗读实际生效的引擎："cloud" | "system" |
    // "system_fallback"（云端失败后回退）。selftest/日志取证用。
    void utteranceStarted(const QString &engineUsed);

private:
    TtsManager();

    // 现有系统引擎路径（含无语音包→默认嗓音兜底链，行为不变）。
    void speakSystem(const QString &text, const QString &langBcp47);
    // 云端 mp3 字节 → QBuffer → QMediaPlayer(FFmpeg 后端) 播放。
    void playCloudAudio(const QByteArray &mp3);

    QTextToSpeech *m_tts = nullptr;
    bool m_available = false;
    int m_voiceCount = 0;
    bool m_unavailableNotified = false;
    // v0.7.1 BUG-A：引擎初始默认嗓音，无专用语音包的语言用它兜底"仍朗读"。
    QVoice m_defaultVoice;

    // v0.7.2 云端引擎（懒创建）；播放链与序号门控防 stop() 后的
    // 迟到回调把旧音频播出来。
    EdgeTtsProvider *m_edge = nullptr;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOut = nullptr;
    QBuffer *m_audioBuf = nullptr;
    QByteArray m_audioData;
    quint64 m_speakSeq = 0;
};
