#pragma once

#include "media_track_info.h"

#include <libaegisub/fs_fwd.h>

#include <functional>
#include <string>
#include <vector>

namespace aegisub_qt {

enum class MediaKind { Video, Audio };

/// Selects the ffms2 track to use when a file has more than one track of the
/// requested kind.
///
/// Installed by the bridges right before they create the original ffms2
/// provider, so `FFmpegSourceProvider::AskForTrackSelection()` (reimplemented in
/// ffmpegsource_common_qt.cpp without any wxWidgets dialog) can resolve the
/// choice synchronously. The callback must return a valid ffms2 track id;
/// negative values fall back to the first track.
using TrackSelector =
    std::function<int(MediaKind, const std::vector<MediaTrackInfo> &tracks)>;

/// Install / remove the selector consulted by the Qt copy of
/// FFmpegSourceProvider::AskForTrackSelection().
void SetTrackSelector(TrackSelector selector);
void ClearTrackSelector();

/// Returns the lowercase log level name, or an empty string for unknown levels.
std::string NormalizeFfmsLogLevel(std::string level);

/// Apply the currently configured FFMS2 log level to the running process.
void ApplyFfmsLogLevel();

/// Enumerate the tracks of `path` of the requested kind without writing an
/// index file. Returns false and fills `error` when the container cannot be
/// opened; an existing container without tracks of that kind returns true with
/// an empty list.
bool ProbeTracks(agi::fs::path const &path, MediaKind kind,
                 std::vector<MediaTrackInfo> *tracks, std::string *error);

} // namespace aegisub_qt
