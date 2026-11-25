#pragma once

#include <cstdint>
#include <memory>

namespace agi { class AudioProvider; }

namespace aegisub_qt {

// The provider must outlive the player. Commands complete on the voice owner
// thread before returning; native failures are reported as exceptions.
class XAudio2PlayerQt final {
public:
    explicit XAudio2PlayerQt(agi::AudioProvider *provider);
    ~XAudio2PlayerQt();

    void Play(int64_t start, int64_t count);
    void Stop();
    bool IsPlaying();
    int64_t GetCurrentPosition();
    void SetVolume(double volume);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aegisub_qt
