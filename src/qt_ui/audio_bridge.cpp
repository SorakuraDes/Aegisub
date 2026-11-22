// wx/agi headers first (the wx precompiled header is force-included into every
// av_qt_core translation unit), then this header (pure C++), then the rest.
#include "../options.h"

#include "audio_bridge.h"

#include "include/aegisub/audio_player.h"
#include "qt_background_runner.h"

#include <libaegisub/audio/provider.h>
#include <libaegisub/exception.h>
#include <libaegisub/fs.h>
#include <libaegisub/log.h>
#include <libaegisub/make_unique.h>
#include <libaegisub/path.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

// Factories defined in the original sources; no headers declare them.
std::unique_ptr<agi::AudioProvider> CreateFFmpegSourceAudioProvider(
    agi::fs::path const &path, agi::BackgroundRunner *runner);
std::unique_ptr<AudioPlayer> CreateXAudio2Player(agi::AudioProvider *provider,
                                                 wxWindow *window);

namespace aegisub_qt {
namespace {

/// UTF-8 -> native path (boost::filesystem::path on Windows is UTF-16).
agi::fs::path pathFromUtf8(std::string const &utf8) {
#ifdef _WIN32
    if (utf8.empty())
        return agi::fs::path();
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
    std::wstring wide(size_t(length > 0 ? length : 0), L'\0');
    if (length > 0)
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), wide.data(), length);
    return agi::fs::path(wide);
#else
    return agi::fs::path(utf8);
#endif
}

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

/// Synthetic audio used by the "Use Dummy Audio" action.
///
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
    std::unique_ptr<AudioPlayer> player;

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

    /// Create the XAudio2 player; on failure keep the wall-clock fallback and
    /// record why. Never throws.
    void CreatePlayer() {
        try {
            player = CreateXAudio2Player(provider.get(), nullptr);
            fallback = false;
            status = "XAudio2";
        } catch (AudioPlayerOpenError const &error) {
            player.reset();
            fallback = true;
            status = "Silent playback (XAudio2 unavailable: " + error.GetMessage() + ")";
            LOG_W("qt/audio") << status;
        } catch (std::exception const &error) {
            player.reset();
            fallback = true;
            status = std::string("Silent playback (XAudio2 unavailable: ") + error.what() + ")";
            LOG_W("qt/audio") << status;
        } catch (...) {
            player.reset();
            fallback = true;
            status = "Silent playback (XAudio2 unavailable)";
            LOG_W("qt/audio") << status;
        }
    }
};

AudioBridge::AudioBridge()
: impl_(agi::make_unique<Impl>())
{
}

AudioBridge::~AudioBridge() = default;

bool AudioBridge::OpenFile(std::string const &utf8_path, std::string *error) {
    Close();
    ensureCacheDirectory();

    try {
        auto provider = CreateFFmpegSourceAudioProvider(pathFromUtf8(utf8_path),
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

    int64_t start = std::clamp<int64_t>(start_sample, 0, total - 1);
    int64_t end = std::clamp<int64_t>(end_sample, start + 1, total);
    if (end <= start) {
        if (error)
            *error = "Invalid playback range";
        return false;
    }

    if (impl_->player) {
        try {
            impl_->player->SetVolume(impl_->volume);
            impl_->player->Play(start, end - start);
            if (impl_->player->IsPlaying())
                return true;
        } catch (std::exception const &err) {
            LOG_W("qt/audio") << "XAudio2 playback failed: " << err.what();
        } catch (...) {
            LOG_W("qt/audio") << "XAudio2 playback failed";
        }

        // The device disappeared or the source voice failed: keep going with the
        // wall-clock head so the timing UI still works.
        impl_->player.reset();
        impl_->fallback = true;
        impl_->status = "Silent playback (XAudio2 playback failed)";
    }

    impl_->fallback = true;
    impl_->fallback_playing = true;
    impl_->fallback_start = start;
    impl_->fallback_end = end;
    impl_->fallback_started = std::chrono::steady_clock::now();
    return true;
}

void AudioBridge::Stop() {
    if (impl_->player)
        impl_->player->Stop();
    impl_->fallback_playing = false;
}

bool AudioBridge::IsPlaying() const {
    if (impl_->player)
        return impl_->player->IsPlaying();
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
        const int64_t position = impl_->player->GetCurrentPosition();
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
        impl_->player->SetVolume(impl_->volume);
}

std::string AudioBridge::PlayerStatus() const { return impl_->status; }

bool AudioBridge::UsingSilentFallback() const { return impl_->fallback; }

} // namespace aegisub_qt
