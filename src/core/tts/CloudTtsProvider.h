// CloudTtsProvider - reserved interface for cloud TTS backends (phase 5).
//
// Phase 3 ships only the local QTextToSpeech engine (TtsManager). Phase 5
// will add cloud voices (e.g. Edge/Azure neural voices) behind this
// interface: TtsManager will own a list of providers and fall back to the
// local engine when no provider can serve the request.
//
// Contract sketch (INTENTIONALLY NOT IMPLEMENTED in phase 3):
//   * synthesize() is asynchronous; the implementation downloads/streams
//     audio for `text` in `langBcp47` and reports completion through the
//     `finished` callback with a playable audio buffer (or an error string).
//   * displayName() feeds the phase-4 settings UI voice picker.
//   * isReachable() lets TtsManager skip offline providers quickly.

#pragma once

#include <QByteArray>
#include <QString>

#include <functional>

class CloudTtsProvider
{
public:
    virtual ~CloudTtsProvider() = default;

    // Human-readable provider name for the settings UI ("Edge TTS", ...).
    virtual QString displayName() const = 0;

    // Quick reachability probe; false lets the caller fall back immediately.
    virtual bool isReachable() const = 0;

    // Asynchronously synthesizes `text`; `finished` receives the encoded
    // audio (e.g. mp3/wav bytes, empty on failure) and an error string
    // (empty on success). Must be invoked on the caller's thread.
    virtual void synthesize(const QString &text, const QString &langBcp47,
                            std::function<void(QByteArray audio,
                                               QString error)> finished) = 0;
};
