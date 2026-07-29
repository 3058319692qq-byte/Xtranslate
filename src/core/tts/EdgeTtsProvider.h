// EdgeTtsProvider - Edge "Read Aloud" 云端免费 TTS（v0.7.2）。
//
// 实现 CloudTtsProvider 契约：走 WebSocket
//   wss://speech.platform.bing.com/consumer/speech/synthesize/readaloud/edge/v1
// 发送 speech.config + SSML(voice+text)，接收 audio/24khz mp3 二进制分片
// 拼接为完整 mp3 交给上层（TtsManager 用 QMediaPlayer/FFmpeg 后端播放）。
//
// TrustedClientToken 是 Edge 浏览器"大声朗读"功能内置的公开固定常量
// （开源 edge-tts 等项目同款），不是用户密钥、不属机密，可硬编码；
// 该接口为非官方免费接口（与 Google/Bing 免费翻译同性质），仅供个人学习。
//
// 失败语义：网络不通/握手被拒/超时/接口变更一律通过 finished 回调返回
// 非空 error（绝不崩溃、绝不静默），由 TtsManager 回退系统嗓音。
// 代理：QWebSocket 走 Qt 应用级代理（TranslationManager::applyProxyFromConfig
// 写入的 QNetworkProxy 全局配置），与翻译链路同源生效。
//
// 环境变量 XT_TTS_FORCE_CLOUD_FAIL=1 时 synthesize 立即失败（--selftest tts
// 用它确定性地证明"云端失败→回退系统嗓音"链路可达，不依赖真实断网）。

#pragma once

#include "core/tts/CloudTtsProvider.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

class QWebSocket;
class QTimer;

class EdgeTtsProvider : public QObject, public CloudTtsProvider
{
    Q_OBJECT
public:
    explicit EdgeTtsProvider(QObject *parent = nullptr);
    ~EdgeTtsProvider() override;

    QString displayName() const override;
    bool isReachable() const override;

    // 单飞行请求：新 synthesize 会中止上一个未完成请求（其 finished 收到
    // "cancelled"）。finished 在 GUI 线程回调。
    void synthesize(const QString &text, const QString &langBcp47,
                    std::function<void(QByteArray audio,
                                       QString error)> finished) override;

    // lang -> 默认 Edge 神经嗓音（内置映射表；配置 tts.cloud_voice.<lang>
    // 覆盖优先）。映射缺失时回退 en-US-AriaNeural（多语言兜底）。
    static QString voiceForLang(const QString &langBcp47);

    // 该语言的候选 Edge 嗓音列表（设置页"云端引擎"嗓音下拉数据源）。
    static QStringList voicesForLang(const QString &langBcp47);

    // 内置映射表条数（--selftest tts 报告 edge_voice_map 字段）。
    static int voiceMapSize();

    // 云端嗓音偏好的配置键（按语言族 tts.cloud_voice.<lang>，与系统引擎的
    // tts.voice.<lang> 平行，互不干扰）。
    static QString voiceConfigKey(const QString &langBcp47);

private:
    void abortCurrent(const QString &reason);
    void finishCurrent(const QByteArray &audio, const QString &error);

    QWebSocket *m_socket = nullptr;
    QTimer *m_timeout = nullptr;
    QByteArray m_audio;
    std::function<void(QByteArray, QString)> m_finished;
};
