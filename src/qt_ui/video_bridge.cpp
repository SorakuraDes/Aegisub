#include "video_bridge.h"

#include "include/aegisub/subtitles_provider.h"
#include "include/aegisub/video_provider.h"

#include "qt_background_runner.h"
#include "subtitles_provider_libass.h"
#include "video_frame.h"
#include "video_provider_dummy.h"

#include <libaegisub/background_runner.h>
#include <libaegisub/color.h>
#include <libaegisub/exception.h>
#include <libaegisub/fs.h>
#include <libaegisub/log.h>
#include <libaegisub/make_unique.h>

#include <cstring>
#include <exception>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

/// agi::fs::path takes a native (narrow) string on Windows, which would mangle
/// non-ASCII video paths; build it from UTF-16 instead.
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

} // namespace

// The original factories live in the global namespace and have no headers.
std::unique_ptr<VideoProvider> CreateFFmpegSourceVideoProvider(
    agi::fs::path const &path, std::string const &colormatrix, agi::BackgroundRunner *br);
std::unique_ptr<VideoProvider> CreateCacheVideoProvider(std::unique_ptr<VideoProvider> provider);

namespace aegisub_qt {

struct VideoBridge::Impl {
    std::unique_ptr<agi::BackgroundRunner> runner;
    std::unique_ptr<VideoProvider> provider;
    std::unique_ptr<SubtitlesProvider> subtitles;
    VideoFrame frame;

    Impl()
    : runner(agi::make_unique<QtBackgroundRunner>())
    {
    }
};

VideoBridge::VideoBridge()
: impl_(agi::make_unique<Impl>())
{
}

VideoBridge::~VideoBridge() {
    // The providers store a raw pointer to the runner, so drop them first.
    impl_->subtitles.reset();
    impl_->provider.reset();
}

void VideoBridge::Initialize() {
    static std::once_flag once;
    std::call_once(once, [] {
        // Initialises the libass library and kicks off the (slow) font index
        // build on libaegisub's background dispatch thread.
        libass::CacheFonts();
    });
}

bool VideoBridge::OpenFile(std::string const &utf8_path, std::string *error) {
    Close();
    Initialize();

#ifdef WITH_FFMS2
    try {
        // No colormatrix override on the first open; ffms2 converts using the
        // colour space it reads from the container, exactly like the wx
        // frontend when it has no previous provider to inherit one from.
        auto provider = CreateFFmpegSourceVideoProvider(pathFromUtf8(utf8_path), {}, impl_->runner.get());
        if (!provider) {
            if (error)
                *error = "Failed to create the FFmpegSource video provider";
            return false;
        }
        impl_->provider = CreateCacheVideoProvider(std::move(provider));
        return true;
    } catch (agi::Exception const &ex) {
        impl_->provider.reset();
        if (error)
            *error = ex.GetMessage();
        return false;
    } catch (std::exception const &ex) {
        impl_->provider.reset();
        if (error)
            *error = ex.what();
        return false;
    }
#else
    if (error)
        *error = "This build has no ffms2 support";
    return false;
#endif
}

bool VideoBridge::OpenDummy(int width, int height, double fps, int frame_count,
                            std::string *error) {
    Close();
    Initialize();

    if (width <= 0 || height <= 0 || frame_count <= 0 || fps <= 0.0) {
        if (error)
            *error = "Invalid dummy video parameters";
        return false;
    }

    try {
        impl_->provider = agi::make_unique<DummyVideoProvider>(
            agi::vfr::Framerate(fps), frame_count, width, height,
            agi::Color(31, 31, 31), true);
        return true;
    } catch (agi::Exception const &ex) {
        impl_->provider.reset();
        if (error)
            *error = ex.GetMessage();
        return false;
    } catch (std::exception const &ex) {
        impl_->provider.reset();
        if (error)
            *error = ex.what();
        return false;
    }
}

void VideoBridge::Close() {
    impl_->provider.reset();
    impl_->subtitles.reset();
    impl_->frame = VideoFrame{};
}

bool VideoBridge::IsOpen() const { return impl_->provider != nullptr; }

int VideoBridge::Width() const { return impl_->provider ? impl_->provider->GetWidth() : 0; }
int VideoBridge::Height() const { return impl_->provider ? impl_->provider->GetHeight() : 0; }
int VideoBridge::FrameCount() const { return impl_->provider ? impl_->provider->GetFrameCount() : 0; }
double VideoBridge::FPS() const { return impl_->provider ? impl_->provider->GetFPS().FPS() : 0.0; }
bool VideoBridge::HasAudio() const { return impl_->provider && impl_->provider->HasAudio(); }

std::string VideoBridge::DecoderName() const {
    return impl_->provider ? impl_->provider->GetDecoderName() : std::string();
}

int VideoBridge::TimeAtFrame(int frame) const {
    if (!impl_->provider)
        return 0;
    const int last = impl_->provider->GetFrameCount() - 1;
    return impl_->provider->GetFPS().TimeAtFrame(frame < 0 ? 0 : (frame > last ? last : frame));
}

int VideoBridge::FrameAtTime(int ms) const {
    if (!impl_->provider)
        return 0;
    return impl_->provider->GetFPS().FrameAtTime(ms, agi::vfr::EXACT);
}

int VideoBridge::FrameAtTimeStart(int ms) const {
    if (!impl_->provider)
        return 0;
    return impl_->provider->GetFPS().FrameAtTime(ms, agi::vfr::START);
}

void VideoBridge::LoadSubtitles(AssFile *file) {
    Initialize();

    if (!impl_->subtitles)
        impl_->subtitles = libass::Create(std::string(), impl_->runner.get());

    impl_->subtitles->LoadSubtitles(file);
}

void VideoBridge::ClearSubtitles() { impl_->subtitles.reset(); }

bool VideoBridge::DecodeFrame(int frame, int time_ms, std::string *error) {
    if (!impl_->provider) {
        if (error)
            *error = "No video is open";
        return false;
    }

    const int last = impl_->provider->GetFrameCount() - 1;
    const int target = frame < 0 ? 0 : (frame > last ? last : frame);

    try {
        impl_->provider->GetFrame(target, impl_->frame);

        // subtitles_provider_libass.cpp ignores VideoFrame::pitch and assumes a
        // tightly packed BGRA buffer. Repack here (instead of touching the
        // original file) whenever the provider padded the scanlines.
        const size_t tight_pitch = impl_->frame.width * 4;
        if (impl_->frame.pitch != tight_pitch) {
            std::vector<unsigned char> packed(tight_pitch * impl_->frame.height);
            for (size_t y = 0; y < impl_->frame.height; ++y) {
                std::memcpy(packed.data() + y * tight_pitch,
                            impl_->frame.data.data() + y * impl_->frame.pitch,
                            tight_pitch);
            }
            impl_->frame.data.swap(packed);
            impl_->frame.pitch = tight_pitch;
        }

        if (impl_->subtitles)
            impl_->subtitles->DrawSubtitles(impl_->frame, time_ms / 1000.0);

        return true;
    } catch (agi::Exception const &ex) {
        if (error)
            *error = ex.GetMessage();
        return false;
    } catch (std::exception const &ex) {
        if (error)
            *error = ex.what();
        return false;
    }
}

std::vector<unsigned char> const &VideoBridge::FrameData() const { return impl_->frame.data; }
int VideoBridge::FrameWidth() const { return int(impl_->frame.width); }
int VideoBridge::FrameHeight() const { return int(impl_->frame.height); }
bool VideoBridge::FrameFlipped() const { return impl_->frame.flipped; }

} // namespace aegisub_qt
