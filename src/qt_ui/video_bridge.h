#pragma once

// Pure C++ facade over Aegisub's video/subtitle providers. The header
// deliberately includes neither wxWidgets nor Qt headers: the implementation is
// compiled into av_qt_core with the wxWidgets precompiled header, while the Qt
// view models include this header from a translation unit where agi::signal's
// `slots` member would otherwise clash with Qt's macros.

#include "media_track_info.h"

#include <libaegisub/fs_fwd.h>

#include <memory>
#include <string>
#include <vector>

class AssFile;

namespace aegisub_qt {

/// Owns one video provider plus the libass subtitles provider and turns a frame
/// number into a tightly packed BGRA8 image with the script rendered on top.
///
/// All calls are synchronous and must be made from the GUI thread. The
/// BackgroundRunner used by the underlying providers is a synchronous one, so
/// opening a video can block while ffms2 indexes the file / libass builds its
/// font index.
class VideoBridge final {
public:
    VideoBridge();
    ~VideoBridge();

    VideoBridge(VideoBridge const &) = delete;
    VideoBridge &operator=(VideoBridge const &) = delete;

    /// One-time libass setup (library + font cache). Cheap; the font index is
    /// built on libaegisub's background dispatch thread. Requires
    /// aegisub_qt::InitializeCore() to have run first.
    static void Initialize();

    /// Enumerate the video tracks of a container without opening/decoding it.
    /// An empty list means "no video track"; `error` is only set when the file
    /// itself cannot be opened.
    static std::vector<MediaTrackInfo> ProbeVideoTracks(agi::fs::path const &path,
                                                       std::string *error);

    /// Open a real video file through FFmpegSource (wrapped in the frame
    /// cache). `preferredTrack` is an ffms2 track id from ProbeVideoTracks();
    /// -1 means "auto-select the first video track" and never shows a dialog.
    /// Returns false and fills `error` on failure.
    bool OpenFile(agi::fs::path const &path, std::string *error, int preferredTrack = -1);
    /// Open the dummy (checkerboard) video without touching the filesystem.
    bool OpenDummy(int width, int height, double fps, int frame_count, std::string *error);
    void Close();
    bool IsOpen() const;

    int Width() const;
    int Height() const;
    double DisplayAspectRatio() const;
    std::vector<int> GetKeyframes() const;
    int FrameCount() const;
    /// Average frame rate (exact for CFR sources).
    double FPS() const;
    bool HasAudio() const;
    std::string DecoderName() const;

    /// Start time of `frame` in milliseconds.
    int TimeAtFrame(int frame) const;
    /// Time inside the frame range that makes a line starting on `frame`
    /// actually start there (agi::vfr::START semantics).
    int TimeAtFrameStart(int frame) const;
    /// Time inside the frame range that makes a line ending on `frame`
    /// actually end there (agi::vfr::END semantics).
    int TimeAtFrameEnd(int frame) const;
    /// Frame visible at `ms`.
    int FrameAtTime(int ms) const;
    /// First frame on which a line starting at `ms` is visible (agi::vfr::START
    /// semantics), used when jumping to the active subtitle line.
    int FrameAtTimeStart(int ms) const;
    /// Last frame on which a line ending at `ms` is visible (agi::vfr::END
    /// semantics), used by the Shift Times frame mode.
    int FrameAtTimeEnd(int ms) const;

    /// Replace the libass track with the contents of `file`. Called after every
    /// edit so the preview always shows the current script.
    void LoadSubtitles(AssFile *file);
    /// Drop the libass track (e.g. when the video is closed).
    void ClearSubtitles();

    /// Decode `frame` and alpha-blend the subtitles visible at `time_ms`.
    /// The resulting buffer is available through FrameData() until the next
    /// call; it is always tightly packed (pitch == width * 4) because
    /// libass' blender assumes that.
    bool DecodeFrame(int frame, int time_ms, std::string *error);

    std::vector<unsigned char> const &FrameData() const;
    int FrameWidth() const;
    int FrameHeight() const;
    /// True when the provider's buffer is bottom-up and needs a vertical flip.
    bool FrameFlipped() const;

    /// Keyframe start times in milliseconds (ascending, no duplicates).
    /// Empty for the dummy video and for containers without keyframe info.
    std::vector<int> const &GetKeyframeTimesMs() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aegisub_qt
