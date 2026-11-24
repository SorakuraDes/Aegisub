#pragma once

// Pure C++ facade over Aegisub's audio stack. Compiled into av_qt_core with the
// wx precompiled header (the ffms2 audio provider still lives in src/ and uses
// wx helpers for its multi-track dialog), while the Qt view models only see this
// header, which includes neither wxWidgets nor Qt.

#include "media_track_info.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace aegisub_qt {

/// Owns one agi::AudioProvider plus the XAudio2 player and turns an audio file
/// into waveform peaks and audible playback.
///
/// All calls are synchronous and must be made from the GUI thread. Opening a
/// file can block while ffms2 indexes it; the XAudio2 player runs playback on
/// its own thread. When XAudio2 cannot be initialised (headless session, no
/// output device) the bridge degrades to a wall-clock "silent playback" head so
/// the waveform and timing interactions stay testable.
class AudioBridge final {
public:
    AudioBridge();
    ~AudioBridge();

    AudioBridge(AudioBridge const &) = delete;
    AudioBridge &operator=(AudioBridge const &) = delete;

    /// Enumerate the audio tracks of a container without opening/decoding it.
    /// An empty list means "no audio track"; `error` is only set when the file
    /// itself cannot be opened.
    static std::vector<MediaTrackInfo> ProbeAudioTracks(std::string const &utf8_path,
                                                       std::string *error);

    /// Open a real audio (or audio+video) file: FFmpegSource -> S16 mono
    /// conversion -> RAM/HD cache (per Audio/Cache/Type). `preferredTrack` is an
    /// ffms2 track id from ProbeAudioTracks(); -1 auto-selects the first audio
    /// track and never shows a dialog.
    bool OpenFile(std::string const &utf8_path, std::string *error, int preferredTrack = -1);
    /// Open a synthetic 440 Hz sine (no file needed; used by the automated
    /// checks). The stock DummyAudioProvider ignores its `ln` parameter and only
    /// produces silence/noise, so the bridge synthesises its own signal.
    bool OpenDummy(int duration_seconds = 10, std::string *error = nullptr);
    void Close();
    bool IsOpen() const;

    int SampleRate() const;
    int Channels() const;
    /// Total samples per channel.
    int64_t SampleCount() const;
    int DurationMs() const;

    /// min/max amplitude in [-1, 1] for `width` columns covering
    /// [start_sample, end_sample). Out-of-range or not-yet-decoded samples
    /// become 0; this never throws.
    void ComputePeaks(int64_t start_sample, int64_t end_sample, int width,
                      std::vector<float> &out_min, std::vector<float> &out_max) const;

    bool PlayRange(int64_t start_sample, int64_t end_sample, std::string *error);
    void Stop();
    bool IsPlaying() const;
    /// Approximate sample currently being heard (0 when stopped; a wall-clock
    /// estimate in silent-fallback mode).
    int64_t CurrentSample() const;
    int64_t EndSample() const;
    void SetVolume(double volume);

    /// "XAudio2" or the reason the silent fallback is active.
    std::string PlayerStatus() const;
    bool UsingSilentFallback() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aegisub_qt
