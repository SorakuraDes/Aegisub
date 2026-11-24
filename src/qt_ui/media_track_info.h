#pragma once

#include <string>

namespace aegisub_qt {

/// One selectable ffms2 track: the id the ffms2 providers accept plus the codec
/// name shown in the Qt track picker.
struct MediaTrackInfo {
    int id = -1;
    std::string description;
};

} // namespace aegisub_qt
