#include "xaudio2_player_qt.h"

#include <libaegisub/audio/provider.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#ifdef XAUDIO2_REDIST
#include <xaudio2redist.h>
#else
#include <xaudio2.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace aegisub_qt {
namespace {

void CheckResult(char const *operation, HRESULT result) {
    if (FAILED(result)) {
        char code[16];
        std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(result));
        throw std::runtime_error(std::string(operation) + " failed (HRESULT " + code + ")");
    }
}

class WakeEvent {
public:
    WakeEvent() : handle_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {
        if (!handle_)
            CheckResult("CreateEventW", HRESULT_FROM_WIN32(GetLastError()));
    }
    ~WakeEvent() { CloseHandle(handle_); }

    void Signal() const { SetEvent(handle_); }
    void Wait() const { WaitForSingleObject(handle_, INFINITE); }

private:
    HANDLE handle_;
};

// Native methods are called exclusively on the MTA. Engine callbacks only
// publish a result or return a buffer and wake the worker.
class NativePlayback final : private IXAudio2EngineCallback, private IXAudio2VoiceCallback {
public:
    NativePlayback(agi::AudioProvider &provider, WakeEvent const &wake)
    : provider_(provider), wake_(wake) {}

    ~NativePlayback() {
        ReleaseEngine();
        if (com_initialized_)
            CoUninitialize();
    }

    void Initialize() {
        CheckResult("CoInitializeEx(COINIT_MULTITHREADED)",
                    CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        com_initialized_ = true;
        const auto rate = provider_.GetSampleRate();
        if (rate < XAUDIO2_MIN_SAMPLE_RATE || rate > XAUDIO2_MAX_SAMPLE_RATE)
            throw std::invalid_argument("XAudio2: unsupported provider sample rate " + std::to_string(rate));
        format_.wFormatTag = WAVE_FORMAT_PCM;
        format_.nChannels = 1;
        format_.nSamplesPerSec = static_cast<DWORD>(rate);
        format_.wBitsPerSample = 16;
        format_.nBlockAlign = sizeof(int16_t);
        format_.nAvgBytesPerSec = format_.nSamplesPerSec * format_.nBlockAlign;
        // Four 50 ms blocks, allocated once, independent of the play range.
        for (auto &block : blocks_)
            block.samples.resize(static_cast<size_t>(rate / 20));
        EnsureEngine();
    }

    void Play(int64_t start, int64_t count) {
        Stop();
        position_ = start;
        if (!count)
            return;
        EnsureEngine();
        start_ = next_ = start;
        end_ = start + count;
        CheckResult("IXAudio2::CreateSourceVoice", engine_->CreateSourceVoice(
            &source_, &format_, XAUDIO2_VOICE_NOPITCH, 1.0f, this));
        CheckResult("IXAudio2SourceVoice::SetVolume", source_->SetVolume(volume_));
        Refill();
        CheckResult("IXAudio2SourceVoice::Start", source_->Start());
    }

    void Stop() {
        if (!source_)
            return;
        auto state = ReadPosition();
        if (next_ == end_ && !state.BuffersQueued) {
            position_ = end_;
            DestroySource();
            return;
        }
        const auto result = source_->Stop();
        // DestroyVoice waits for the voice and all callbacks to finish. This
        // also discards queued PCM, without FlushSourceBuffers racing a restart.
        DestroySource();
        CheckResult("IXAudio2SourceVoice::Stop", result);
    }

    bool IsPlaying() {
        UpdatePosition();
        return source_ != nullptr;
    }

    int64_t GetCurrentPosition() {
        UpdatePosition();
        return position_;
    }

    void SetVolume(float volume) {
        if (source_)
            CheckResult("IXAudio2SourceVoice::SetVolume", source_->SetVolume(volume));
        volume_ = volume;
    }

    void Pump() {
        CheckCallbacks();
        UpdatePosition();
        if (source_)
            Refill();
        CheckCallbacks();
    }

    void CheckCallbacks() const {
        CheckResult("IXAudio2EngineCallback::OnCriticalError", critical_error_.load(std::memory_order_acquire));
        CheckResult("IXAudio2VoiceCallback::OnVoiceError", voice_error_.load(std::memory_order_acquire));
    }

    void ReleaseEngine() {
        DestroySource();
        if (master_) {
            master_->DestroyVoice();
            master_ = nullptr;
        }
        if (engine_) {
            if (registered_)
                engine_->UnregisterForCallbacks(this);
            engine_->Release();
            engine_ = nullptr;
        }
        registered_ = false;
        critical_error_.store(S_OK, std::memory_order_relaxed);
        voice_error_.store(S_OK, std::memory_order_relaxed);
    }

private:
    struct Block {
        std::vector<int16_t> samples;
        std::atomic<bool> in_use{false};
    };

    agi::AudioProvider &provider_;
    WakeEvent const &wake_;
    std::array<Block, 4> blocks_;
    WAVEFORMATEX format_{};
    IXAudio2 *engine_ = nullptr;
    IXAudio2MasteringVoice *master_ = nullptr;
    IXAudio2SourceVoice *source_ = nullptr;
    bool registered_ = false;
    bool com_initialized_ = false;
    std::atomic<HRESULT> critical_error_{S_OK};
    std::atomic<HRESULT> voice_error_{S_OK};
    float volume_ = 1.0f;
    int64_t start_ = 0;
    int64_t next_ = 0;
    int64_t end_ = 0;
    int64_t position_ = 0;

    void EnsureEngine() {
        if (engine_)
            return;
        CheckResult("XAudio2Create", XAudio2Create(&engine_, 0, XAUDIO2_DEFAULT_PROCESSOR));
        CheckResult("IXAudio2::RegisterForCallbacks", engine_->RegisterForCallbacks(this));
        registered_ = true;
        CheckResult("IXAudio2::CreateMasteringVoice", engine_->CreateMasteringVoice(&master_));
        CheckCallbacks();
    }

    void DestroySource() {
        if (!source_)
            return;
        source_->DestroyVoice();
        source_ = nullptr;
        for (auto &block : blocks_)
            block.in_use.store(false, std::memory_order_relaxed);
    }

    XAUDIO2_VOICE_STATE ReadPosition() {
        XAUDIO2_VOICE_STATE state{};
        source_->GetState(&state);
        const auto played = std::min(state.SamplesPlayed, static_cast<UINT64>(end_ - start_));
        position_ = start_ + static_cast<int64_t>(played);
        return state;
    }

    void UpdatePosition() {
        if (source_ && !ReadPosition().BuffersQueued && next_ == end_) {
            position_ = end_;
            DestroySource();
        }
    }

    void Refill() {
        for (auto &block : blocks_) {
            if (next_ == end_)
                break;
            if (block.in_use.load(std::memory_order_acquire))
                continue;
            const auto count = std::min(end_ - next_, static_cast<int64_t>(block.samples.size()));
            try {
                provider_.GetInt16MonoAudio(block.samples.data(), next_, count);
            } catch (agi::Exception const &error) {
                throw std::runtime_error("XAudio2 PCM provider: " + error.GetMessage());
            }
            XAUDIO2_BUFFER buffer{};
            // Do not set XAUDIO2_END_OF_STREAM: it resets SamplesPlayed when
            // consumed. This finite range ends when all its frames are played.
            buffer.AudioBytes = static_cast<UINT32>(count * sizeof(int16_t));
            buffer.pAudioData = reinterpret_cast<BYTE const *>(block.samples.data());
            buffer.pContext = &block;
            block.in_use.store(true, std::memory_order_relaxed);
            CheckResult("IXAudio2SourceVoice::SubmitSourceBuffer", source_->SubmitSourceBuffer(&buffer));
            next_ += count;
        }
    }

    void PublishError(std::atomic<HRESULT> &slot, HRESULT error) {
        HRESULT expected = S_OK;
        slot.compare_exchange_strong(expected, error, std::memory_order_release, std::memory_order_relaxed);
        wake_.Signal();
    }

    void STDMETHODCALLTYPE OnProcessingPassStart() override {}
    void STDMETHODCALLTYPE OnProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnCriticalError(HRESULT error) override { PublishError(critical_error_, error); }
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnStreamEnd() override {}
    void STDMETHODCALLTYPE OnBufferStart(void *) override {}
    void STDMETHODCALLTYPE OnBufferEnd(void *context) override {
        static_cast<Block *>(context)->in_use.store(false, std::memory_order_release);
        wake_.Signal();
    }
    void STDMETHODCALLTYPE OnLoopEnd(void *) override {}
    void STDMETHODCALLTYPE OnVoiceError(void *, HRESULT error) override { PublishError(voice_error_, error); }
};

} // namespace

struct XAudio2PlayerQt::Impl {
    enum class Command { Initialize, Play, Stop, IsPlaying, GetPosition, SetVolume };

    struct Request {
        Command command;
        int64_t start;
        int64_t count;
        float volume;
        std::promise<int64_t> result;
    };

    agi::AudioProvider &provider;
    WakeEvent wake;
    std::mutex command_mutex;
    std::atomic<Request *> request{nullptr};
    std::atomic<bool> shutdown{false};
    std::thread worker;

    explicit Impl(agi::AudioProvider &provider)
    : provider(provider), worker([this] { Run(); }) {
        try {
            Invoke(Command::Initialize);
        } catch (...) {
            Join();
            throw;
        }
    }

    ~Impl() { Join(); }

    void Join() {
        shutdown.store(true, std::memory_order_release);
        wake.Signal();
        worker.join();
    }

    int64_t Invoke(Command command, int64_t start = 0, int64_t count = 0, float volume = 0) {
        std::lock_guard<std::mutex> lock(command_mutex);
        Request pending{command, start, count, volume, {}};
        auto result = pending.result.get_future();
        request.store(&pending, std::memory_order_release);
        wake.Signal();
        return result.get();
    }

    void Run() {
        NativePlayback playback(provider, wake);
        std::exception_ptr streaming_failure;
        for (;;) {
            wake.Wait();
            if (shutdown.load(std::memory_order_acquire))
                break;
            if (auto *pending = request.exchange(nullptr, std::memory_order_acq_rel)) {
                int64_t result = 0;
                try {
                    if (streaming_failure) {
                        auto failure = streaming_failure;
                        streaming_failure = nullptr;
                        std::rethrow_exception(failure);
                    }
                    playback.CheckCallbacks();
                    switch (pending->command) {
                        case Command::Initialize: playback.Initialize(); break;
                        case Command::Play: playback.Play(pending->start, pending->count); break;
                        case Command::Stop: playback.Stop(); break;
                        case Command::IsPlaying: result = playback.IsPlaying(); break;
                        case Command::GetPosition: result = playback.GetCurrentPosition(); break;
                        case Command::SetVolume: playback.SetVolume(pending->volume); break;
                    }
                    playback.CheckCallbacks();
                } catch (...) {
                    auto failure = std::current_exception();
                    playback.ReleaseEngine();
                    pending->result.set_exception(failure);
                    continue;
                }
                pending->result.set_value(result);
            }
            try {
                playback.Pump();
            } catch (...) {
                streaming_failure = std::current_exception();
                playback.ReleaseEngine();
            }
        }
    }
};

XAudio2PlayerQt::XAudio2PlayerQt(agi::AudioProvider *provider) {
    if (!provider)
        throw std::invalid_argument("XAudio2: missing audio provider");
    impl_.reset(new Impl(*provider));
}

XAudio2PlayerQt::~XAudio2PlayerQt() = default;

void XAudio2PlayerQt::Play(int64_t start, int64_t count) {
    const auto total = impl_->provider.GetNumSamples();
    if (start < 0 || count < 0 || start > total || count > total - start)
        throw std::out_of_range("XAudio2: playback range is outside the audio provider");
    impl_->Invoke(Impl::Command::Play, start, count);
}

void XAudio2PlayerQt::Stop() {
    impl_->Invoke(Impl::Command::Stop);
}

bool XAudio2PlayerQt::IsPlaying() {
    return impl_->Invoke(Impl::Command::IsPlaying) != 0;
}

int64_t XAudio2PlayerQt::GetCurrentPosition() {
    return impl_->Invoke(Impl::Command::GetPosition);
}

void XAudio2PlayerQt::SetVolume(double volume) {
    if (!std::isfinite(volume) || std::abs(volume) > XAUDIO2_MAX_VOLUME_LEVEL)
        throw std::out_of_range("XAudio2: playback volume is out of range");
    impl_->Invoke(Impl::Command::SetVolume, 0, 0, static_cast<float>(volume));
}

} // namespace aegisub_qt
