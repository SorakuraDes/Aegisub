#include "video_bridge.h"

#include "ffmpegsource_common_qt.h"

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

// The original factories live in the global namespace and have no headers.
std::unique_ptr<VideoProvider> CreateFFmpegSourceVideoProvider(
    agi::fs::path const &path, std::string const &colormatrix, agi::BackgroundRunner *br);
std::unique_ptr<VideoProvider> CreateCacheVideoProvider(std::unique_ptr<VideoProvider> provider);

namespace {

/// Installs the ffms2 track selector for the duration of one provider creation.
///
/// The Qt copy of FFmpegSourceProvider::AskForTrackSelection() consults it
/// instead of popping the original wxWidgets choice dialog, so the Qt process
/// never needs a wxApp for multi-track files.
class TrackSelectorGuard {
public:
    explicit TrackSelectorGuard(int preferred_track) {
        aegisub_qt::SetTrackSelector(
            [preferred_track](aegisub_qt::MediaKind,
                              const std::vector<aegisub_qt::MediaTrackInfo> &tracks) {
                if (preferred_track >= 0)
                    return preferred_track;
                return tracks.empty() ? -1 : tracks.front().id;
            });
    }

    ~TrackSelectorGuard() { aegisub_qt::ClearTrackSelector(); }

    TrackSelectorGuard(TrackSelectorGuard const &) = delete;
    TrackSelectorGuard &operator=(TrackSelectorGuard const &) = delete;
};

/// Keyframe start times in milliseconds (agi::vfr::START semantics), ascending
/// and de-duplicated.
std::vector<int> CollectKeyframeTimes(VideoProvider &provider) {
    std::vector<int> times;
    const int frame_count = provider.GetFrameCount();
    if (frame_count <= 0)
        return times;

    const auto fps = provider.GetFPS();
    const auto frames = provider.GetKeyFrames();
    times.reserve(frames.size());
    for (int frame : frames) {
        if (frame < 0 || frame >= frame_count)
            continue;
        // agi::vfr::START semantics gives the time at which a line starting on
        // this frame is first visible; for the first frame that can be slightly
        // negative, which is clamped to 0 for drawing/snapping.
        const int ms = std::max(0, fps.TimeAtFrame(frame, agi::vfr::START));
        if (!times.empty() && ms <= times.back())
            continue;
        times.push_back(ms);
    }
    return times;
}

} // namespace

namespace aegisub_qt {

struct VideoBridge::Impl {
    std::unique_ptr<agi::BackgroundRunner> runner;
    std::unique_ptr<VideoProvider> provider;
    std::unique_ptr<SubtitlesProvider> subtitles;
    VideoFrame frame;
    std::vector<int> keyframe_times_ms;

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

bool VideoBridge::OpenFile(agi::fs::path const &path, std::string *error,
                           int preferredTrack) {
    Close();
    Initialize();

#ifdef WITH_FFMS2
    try {
        // No colormatrix override on the first open; ffms2 converts using the
        // colour space it reads from the container, exactly like the wx
        // frontend when it has no previous provider to inherit one from.
        // The guard makes multi-track files pick a track without a wx dialog.
        TrackSelectorGuard selector(preferredTrack);
        auto provider = CreateFFmpegSourceVideoProvider(path, {}, impl_->runner.get());
        if (!provider) {
            if (error)
                *error = "Failed to create the FFmpegSource video provider";
            return false;
        }
        impl_->provider = CreateCacheVideoProvider(std::move(provider));
        impl_->keyframe_times_ms = CollectKeyframeTimes(*impl_->provider);
        return true;
    } catch (agi::Exception const &ex) {
        impl_->provider.reset();
        impl_->keyframe_times_ms.clear();
        if (error)
            *error = ex.GetMessage();
        return false;
    } catch (std::exception const &ex) {
        impl_->provider.reset();
        impl_->keyframe_times_ms.clear();
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

std::vector<MediaTrackInfo> VideoBridge::ProbeVideoTracks(agi::fs::path const &path,
                                                          std::string *error) {
    std::vector<MediaTrackInfo> tracks;
    ProbeTracks(path, MediaKind::Video, &tracks, error);
    return tracks;
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
        // The dummy provider reports no keyframes (GetKeyFrames() == {}).
        impl_->keyframe_times_ms.clear();
        return true;
    } catch (agi::Exception const &ex) {
        impl_->provider.reset();
        impl_->keyframe_times_ms.clear();
        if (error)
            *error = ex.GetMessage();
        return false;
    } catch (std::exception const &ex) {
        impl_->provider.reset();
        impl_->keyframe_times_ms.clear();
        if (error)
            *error = ex.what();
        return false;
    }
}

void VideoBridge::Close() {
    impl_->provider.reset();
    impl_->subtitles.reset();
    impl_->frame = VideoFrame{};
    impl_->keyframe_times_ms.clear();
}

bool VideoBridge::IsOpen() const { return impl_->provider != nullptr; }

int VideoBridge::Width() const { return impl_->provider ? impl_->provider->GetWidth() : 0; }
int VideoBridge::Height() const { return impl_->provider ? impl_->provider->GetHeight() : 0; }
double VideoBridge::DisplayAspectRatio() const {
    if (!IsOpen() || Height() <= 0) return 0.0;
    const double dar = impl_->provider->GetDAR();
    return dar > 0.0 ? dar : double(Width()) / Height();
}
std::vector<int> VideoBridge::GetKeyframes() const {
    return IsOpen() ? impl_->provider->GetKeyFrames() : std::vector<int>{};
}
int VideoBridge::FrameCount() const { return impl_->provider ? impl_->provider->GetFrameCount() : 0; }
double VideoBridge::FPS() const { return impl_->provider ? impl_->provider->GetFPS().FPS() : 0.0; }
agi::vfr::Framerate VideoBridge::FramerateSnapshot() const {
    return impl_->provider ? impl_->provider->GetFPS() : agi::vfr::Framerate{};
}
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

int VideoBridge::TimeAtFrameStart(int frame) const {
    if (!impl_->provider)
        return 0;
    return impl_->provider->GetFPS().TimeAtFrame(frame, agi::vfr::START);
}

int VideoBridge::TimeAtFrameEnd(int frame) const {
    if (!impl_->provider)
        return 0;
    return impl_->provider->GetFPS().TimeAtFrame(frame, agi::vfr::END);
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

int VideoBridge::FrameAtTimeEnd(int ms) const {
    if (!impl_->provider)
        return 0;
    return impl_->provider->GetFPS().FrameAtTime(ms, agi::vfr::END);
}

void VideoBridge::LoadSubtitles(AssFile *file) {
    // libass's LoadSubtitles can throw (agi::InternalError etc.) and this call
    // runs on the GUI thread from a ViewModel slot; an escaping exception would
    // end in std::terminate -> abort (S14 crash class). Drop the broken
    // provider state instead so the next commit/reload can retry.
    try {
        Initialize();

        if (!impl_->subtitles)
            impl_->subtitles = libass::Create(std::string(), impl_->runner.get());

        impl_->subtitles->LoadSubtitles(file);
    } catch (agi::Exception const &ex) {
        LOG_E("qt/video") << "Failed to load subtitles: " << ex.GetMessage();
        impl_->subtitles.reset();
    } catch (std::exception const &ex) {
        LOG_E("qt/video") << "Failed to load subtitles: " << ex.what();
        impl_->subtitles.reset();
    } catch (...) {
        LOG_E("qt/video") << "Failed to load subtitles: unknown error";
        impl_->subtitles.reset();
    }
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

std::vector<int> const &VideoBridge::GetKeyframeTimesMs() const {
    return impl_->keyframe_times_ms;
}

} // namespace aegisub_qt
