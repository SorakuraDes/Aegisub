// wx/agi headers first (the wx precompiled header is force-included into every
// av_qt_core translation unit), then this header (pure C++), then the rest.
#include "../options.h"

#include "audio_bridge.h"

#include "ffmpegsource_common_qt.h"

#include "xaudio2_player_qt.h"
#include "qt_background_runner.h"

#include <libaegisub/audio/provider.h>
#include <libaegisub/exception.h>
#include <libaegisub/fs.h>
#include <libaegisub/log.h>
#include <libaegisub/make_unique.h>
#include <libaegisub/path.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <complex>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// Factories defined in the original sources; no headers declare them.
std::unique_ptr<agi::AudioProvider> CreateFFmpegSourceAudioProvider(
    agi::fs::path const &path, agi::BackgroundRunner *runner);

namespace aegisub_qt {
namespace {

/// FFmpegSourceProvider::GetCacheFilename() creates the ffms2 cache directory
/// through agi::fs::CreateDirectory (which can fail-fast in this environment,
/// see docs/qt6-frontend.md pitfall 5). Pre-create it with std::filesystem.
void ensureCacheDirectory() {
#ifdef _WIN32
    try {
        if (!config::path)
            return;
        const auto directory = config::path->Decode("?local/ffms2cache");
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(directory.wstring()), error);
    } catch (...) {
        // Indexing will simply be repeated next time; opening must not abort.
    }
#endif
}

/// Installs the ffms2 track selector for the duration of one provider creation.
/// The Qt copy of FFmpegSourceProvider::AskForTrackSelection() consults it
/// instead of popping the original wxWidgets choice dialog.
class TrackSelectorGuard {
public:
    explicit TrackSelectorGuard(int preferred_track) {
        SetTrackSelector(
            [preferred_track](MediaKind,
                              const std::vector<MediaTrackInfo> &tracks) {
                if (preferred_track >= 0)
                    return preferred_track;
                return tracks.empty() ? -1 : tracks.front().id;
            });
    }

    ~TrackSelectorGuard() { ClearTrackSelector(); }

    TrackSelectorGuard(TrackSelectorGuard const &) = delete;
    TrackSelectorGuard &operator=(TrackSelectorGuard const &) = delete;
};

/// Synthetic audio used by the "Use Dummy Audio" action.///
/// The stock agi::AudioProvider dummy supports only silence/noise and ignores
/// its documented `ln` parameter (it always reports 2.5 hours), which makes it
/// useless for a waveform check. This provider generates a 440 Hz sine with a
/// slow amplitude envelope over a short, bounded length.
class SyntheticSineProvider final : public agi::AudioProvider {
public:
    SyntheticSineProvider(int rate, int duration_seconds) {
        channels = 1;
        sample_rate = rate;
        bytes_per_sample = int(sizeof(int16_t));
        float_samples = false;
        num_samples = int64_t(std::max(1, duration_seconds)) * rate;
        decoded_samples = num_samples;
    }

    void FillBuffer(void *buf, int64_t start, int64_t count) const override {
        auto *samples = static_cast<int16_t *>(buf);
        constexpr double two_pi = 6.28318530717958647692;
        const double envelope_hz = 0.5;
        const double tone_hz = 440.0;
        for (int64_t i = 0; i < count; ++i) {
            const double t = double(start + i) / sample_rate;
            const double envelope = 0.35 + 0.4 * std::sin(two_pi * envelope_hz * t);
            const double value = 15000.0 * envelope * std::sin(two_pi * tone_hz * t);
            samples[i] = static_cast<int16_t>(std::clamp(value, -32767.0, 32767.0));
        }
    }
};

} // namespace

struct AudioBridge::Impl {
    std::unique_ptr<agi::BackgroundRunner> runner;
    std::unique_ptr<agi::AudioProvider> provider;
    std::unique_ptr<XAudio2PlayerQt> player;

    template<class Operation>
    bool UsePlayer(char const *operation, Operation &&run) {
        try {
            run(*player);
            return true;
        } catch (agi::Exception const &error) {
            PlayerFailed(error.GetMessage());
        } catch (std::exception const &error) {
            PlayerFailed(error.what());
        } catch (...) {
            PlayerFailed(std::string("XAudio2 ") + operation + " failed: unknown error");
        }
        return false;
    }

    bool fallback = false;
    bool fallback_playing = false;
    int64_t fallback_start = 0;
    int64_t fallback_end = 0;
    std::chrono::steady_clock::time_point fallback_started;
    double volume = 1.0;
    std::string status = "No audio output";

    Impl()
    : runner(agi::make_unique<QtBackgroundRunner>())
    {
    }

    void PlayerFailed(std::string const &reason) {
        player.reset();
        fallback = true;
        status = "Silent playback (" + reason + ")";
        LOG_W("qt/audio") << status;
    }

    void CreatePlayer() {
        try {
            player = agi::make_unique<XAudio2PlayerQt>(provider.get());
            fallback = false;
            status = "XAudio2";
        } catch (agi::Exception const &error) {
            PlayerFailed(error.GetMessage());
        } catch (std::exception const &error) {
            PlayerFailed(error.what());
        } catch (...) {
            PlayerFailed("XAudio2 initialization failed: unknown error");
        }
    }
};

AudioBridge::AudioBridge()
: impl_(agi::make_unique<Impl>())
{
}

AudioBridge::~AudioBridge() = default;

bool AudioBridge::OpenFile(agi::fs::path const &path, std::string *error,
                           int preferredTrack) {
    Close();
    ensureCacheDirectory();

    try {
        // Multi-track files choose the track through the injected selector, so
        // no wx dialog is ever created from the Qt process.
        TrackSelectorGuard selector(preferredTrack);
        auto provider = CreateFFmpegSourceAudioProvider(path,
                                                        impl_->runner.get());
        if (!provider) {
            if (error)
                *error = "The audio provider returned no data";
            return false;
        }

        // Aegisub's playback pipeline works on 16-bit mono audio; this also
        // doubles low sample rates to at least 32 kHz.
        provider = CreateConvertAudioProvider(std::move(provider));

        int cache_type = 1;
        try {
            cache_type = OPT_GET("Audio/Cache/Type")->GetInt();
        } catch (...) {
        }
        if (cache_type == 1)
            provider = CreateRAMAudioProvider(std::move(provider));
        else
            provider = CreateLockAudioProvider(std::move(provider));

        impl_->provider = std::move(provider);
        impl_->CreatePlayer();
        return true;
    } catch (agi::AudioDataNotFound const &err) {
        if (error)
            *error = "The file contains no audio data: " + err.GetMessage();
    } catch (agi::AudioProviderError const &err) {
        if (error)
            *error = err.GetMessage();
    } catch (agi::Exception const &err) {
        if (error)
            *error = err.GetMessage();
    } catch (std::exception const &err) {
        if (error)
            *error = err.what();
    }

    Close();
    return false;
}

bool AudioBridge::OpenDummy(int duration_seconds, std::string *error) {
    Close();
    if (duration_seconds <= 0)
        duration_seconds = 10;

    try {
        impl_->provider = agi::make_unique<SyntheticSineProvider>(44100, duration_seconds);
        impl_->CreatePlayer();
        return true;
    } catch (std::exception const &err) {
        if (error)
            *error = err.what();
        Close();
        return false;
    }
}

void AudioBridge::Close() {
    impl_->player.reset();
    impl_->provider.reset();
    impl_->fallback = false;
    impl_->fallback_playing = false;
    impl_->fallback_start = 0;
    impl_->fallback_end = 0;
    impl_->status = "No audio output";
}

std::vector<MediaTrackInfo> AudioBridge::ProbeAudioTracks(agi::fs::path const &path,
                                                          std::string *error) {
    std::vector<MediaTrackInfo> tracks;
    ProbeTracks(path, MediaKind::Audio, &tracks, error);
    return tracks;
}

bool AudioBridge::IsOpen() const { return impl_->provider != nullptr; }

int AudioBridge::SampleRate() const {
    return impl_->provider ? impl_->provider->GetSampleRate() : 0;
}

int AudioBridge::Channels() const {
    return impl_->provider ? impl_->provider->GetChannels() : 0;
}

int64_t AudioBridge::SampleCount() const {
    return impl_->provider ? impl_->provider->GetNumSamples() : 0;
}

int AudioBridge::DurationMs() const {
    if (!impl_->provider || impl_->provider->GetSampleRate() <= 0)
        return 0;
    const int64_t count = impl_->provider->GetNumSamples();
    return int(count * 1000 / impl_->provider->GetSampleRate());
}

void AudioBridge::ComputePeaks(int64_t start_sample, int64_t end_sample, int width,
                               std::vector<float> &out_min, std::vector<float> &out_max) const {
    const int columns = std::max(0, width);
    out_min.assign(size_t(columns), 0.0f);
    out_max.assign(size_t(columns), 0.0f);
    if (!impl_->provider || columns == 0)
        return;

    const int64_t total = impl_->provider->GetNumSamples();

    // The requested range may extend past the end of the stream (the viewport
    // shows trailing silence). Columns are mapped over the *requested* range and
    // columns with no samples stay at zero, so the waveform is not stretched to
    // fill the whole view.
    const int64_t requested_start = std::max<int64_t>(0, start_sample);
    const int64_t requested_end = std::max<int64_t>(requested_start + 1, end_sample);
    int64_t read_start = std::min<int64_t>(requested_start, total);
    int64_t read_end = std::min<int64_t>(requested_end, total);

    // Keep the temporary buffer bounded; the viewport can only show at most a
    // few million samples at the minimum zoom level.
    constexpr int64_t max_samples = int64_t(8) << 20;
    if (read_end - read_start > max_samples)
        read_end = read_start + max_samples;

    try {
        std::vector<int16_t> samples;
        if (read_end > read_start) {
            samples.resize(size_t(read_end - read_start));
            impl_->provider->GetInt16MonoAudio(samples.data(), read_start,
                                               read_end - read_start);
        }

        const double per_column = double(requested_end - requested_start) / columns;
        for (int x = 0; x < columns; ++x) {
            int64_t first = requested_start + int64_t(x * per_column);
            int64_t last = requested_start + int64_t((x + 1) * per_column);
            if (last <= first)
                last = first + 1;
            first = std::clamp<int64_t>(first, read_start, read_end);
            last = std::clamp<int64_t>(last, read_start, read_end);

            float minimum = 0.0f;
            float maximum = 0.0f;
            for (int64_t i = first; i < last; ++i) {
                const float value = float(samples[size_t(i - read_start)]) / 32768.0f;
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
            }
            out_min[size_t(x)] = minimum;
            out_max[size_t(x)] = maximum;
        }
    } catch (...) {
        // A decode failure on a partially cached region must not crash the UI;
        // the affected columns stay at zero.
        LOG_D("qt/audio") << "Peak extraction failed";
    }
}

int64_t AudioBridge::DecodedSamples() const {
    return impl_->provider ? impl_->provider->GetDecodedSamples() : 0;
}

void AudioBridge::ComputeSpectrum(int64_t start, int64_t end, int width,
                                  std::vector<float> &levels) const try {
    levels.clear();
    constexpr int n = 2 * (spectrumBins - 1);
    constexpr double tau = 6.28318530717958647692;
    struct Plan {
        std::array<int, n> reversed{};
        std::array<float, n> window{};
        std::array<std::complex<float>, n / 2> roots{};
        Plan() {
            for (int i = 0; i < n; ++i) {
                reversed[i] = (reversed[i >> 1] >> 1) | ((i & 1) * (n / 2));
                window[i] = float(0.5 - 0.5 * std::cos(tau * i / n));
            }
            for (int i = 0; i < n / 2; ++i)
                roots[i] = std::polar(1.0f, float(-tau * i / n));
        }
    };
    static const Plan plan;
    levels.assign(size_t(std::max(0, width)) * spectrumBins, 0.0f);
    if (!impl_->provider || width <= 0 || end <= start) return;
    const auto available = DecodedSamples();
    std::array<int16_t, n> pcm;
    std::array<std::complex<float>, n> frame;
    for (int x = 0; x < width; ++x) {
        const int64_t center = start + int64_t((x + 0.5) * (double(end) - start) / width);
        if (center < 0 || center >= available) continue;
        const auto left = center - n / 2;
        const auto first = std::max<int64_t>(0, left);
        const auto last = std::min<int64_t>(available, left + n);
        pcm.fill(0);
        impl_->provider->GetInt16MonoAudio(pcm.data() + (first - left), first, last - first);
        for (int i = 0; i < n; ++i)
            frame[plan.reversed[i]] = float(pcm[i]) * plan.window[i] / 32768.0f;
        for (int span = 2; span <= n; span *= 2) {
            for (int base = 0; base < n; base += span) {
                for (int j = 0; j < span / 2; ++j) {
                    auto &a = frame[base + j];
                    auto &b = frame[base + j + span / 2];
                    const auto odd = b * plan.roots[j * (n / span)];
                    b = a - odd;
                    a += odd;
                }
            }
        }
        for (int k = 0; k < spectrumBins; ++k) {
            const float gain = (k == 0 || k == n / 2) ? 2.0f / n : 4.0f / n;
            const float db = 20.0f * std::log10(std::max(0.0001f, std::abs(frame[k]) * gain));
            levels[size_t(x) * spectrumBins + k] = std::clamp((db + 80.0f) / 80.0f, 0.0f, 1.0f);
        }
    }
} catch (agi::Exception const &) {
    std::fputs("[qt/audio] Spectrum extraction failed (Aegisub exception)\n", stderr);
} catch (std::exception const &e) {
    std::fprintf(stderr, "[qt/audio] Spectrum extraction failed: %s\n", e.what());
} catch (...) {
    std::fputs("[qt/audio] Spectrum extraction failed (unknown exception)\n", stderr);
}

bool AudioBridge::PlayRange(int64_t start_sample, int64_t end_sample, std::string *error) {
    Stop();

    if (!impl_->provider) {
        if (error)
            *error = "No audio is open";
        return false;
    }

    const int64_t total = impl_->provider->GetNumSamples();
    if (total <= 0) {
        if (error)
            *error = "The audio stream is empty";
        return false;
    }

    const int64_t start = std::clamp<int64_t>(start_sample, 0, total);
    const int64_t end = std::clamp<int64_t>(end_sample, 0, total);
    if (end <= start) {
        if (error)
            *error = "Invalid playback range";
        return false;
    }
    impl_->fallback_end = end;

    // Retry on the next explicit Play after device loss, without reopening
    // and reindexing the media.
    if (!impl_->player)
        impl_->CreatePlayer();
    if (impl_->player && impl_->UsePlayer("start", [&](auto &player) {
        player.SetVolume(impl_->volume);
        player.Play(start, end - start);
        LOG_D("qt/audio") << "XAudio2 started: sample=" << start
                          << " count=" << end - start << " rate=" << SampleRate()
                          << " volume=" << impl_->volume;
    }))
        return true;

    // Keep timing tools usable on systems with no output device. The status
    // retains the exact backend error instead of presenting this as real audio.
    impl_->fallback_playing = true;
    impl_->fallback_start = start;
    impl_->fallback_started = std::chrono::steady_clock::now();
    return true;
}

void AudioBridge::Stop() {
    if (impl_->player)
        impl_->UsePlayer("stop", [](auto &player) { player.Stop(); });
    impl_->fallback_playing = false;
}

bool AudioBridge::IsPlaying() const {
    if (impl_->player) {
        bool playing = false;
        impl_->UsePlayer("read state", [&](auto &player) { playing = player.IsPlaying(); });
        return playing;
    }
    if (!impl_->fallback_playing)
        return false;

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - impl_->fallback_started)
                             .count();
    const int64_t played = int64_t(elapsed) * impl_->provider->GetSampleRate() / 1000;
    return impl_->fallback_start + played < impl_->fallback_end;
}

int64_t AudioBridge::CurrentSample() const {
    if (impl_->player) {
        int64_t position = 0;
        impl_->UsePlayer("read position", [&](auto &player) { position = player.GetCurrentPosition(); });
        return position > 0 ? position : 0;
    }

    if (!impl_->fallback_playing || !impl_->provider)
        return 0;

    try {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - impl_->fallback_started)
                                 .count();
        const int64_t position =
            impl_->fallback_start + int64_t(elapsed) * impl_->provider->GetSampleRate() / 1000;
        return std::clamp<int64_t>(position, impl_->fallback_start, impl_->fallback_end);
    } catch (...) {
        return 0;
    }
}

int64_t AudioBridge::EndSample() const { return impl_->fallback_end; }

void AudioBridge::SetVolume(double volume) {
    impl_->volume = std::clamp(volume, 0.0, 1.0);
    if (impl_->player)
        impl_->UsePlayer("set volume", [&](auto &player) { player.SetVolume(impl_->volume); });
}

std::string AudioBridge::PlayerStatus() const { return impl_->status; }

bool AudioBridge::UsingSilentFallback() const { return impl_->fallback; }

} // namespace aegisub_qt
