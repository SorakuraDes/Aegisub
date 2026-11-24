#pragma once

#include <string>
#include <string_view>
#include <utility>

class AssDialogue;
class AssStyle;

namespace aegisub_qt {

// Read a top-level pos, falling back to a move start. Coordinates are left
// unchanged when neither is found. Optional outputs may be null.
bool parsePos(std::string_view text, double* x, double* y, bool* explicitPos = nullptr);
// Read the last valid top-level rectangular clip, ignoring vectors and iclip.
bool parseRectClip(std::string_view text, int* x1, int* y1, int* x2, int* y2);

// Set a static position, removing competing top-level pos/move tags.
// Nonfinite coordinates leave the text unchanged.
std::string setPosTag(std::string_view text, double x, double y);
// Replace existing ordinary rectangles; never edit vector or inverse clips.
// Do not add a rectangle when only a vector or inverse clip is present.
std::string setRectClipTag(std::string_view text, int x1, int y1, int x2, int y2);
std::string removeRectClipTag(std::string_view text);

// wx visual-tool anchor: explicit pos, then move start, then alignment/margins.
std::pair<double, double> defaultAnchorPos(const AssDialogue& line, const AssStyle* style,
                                           int scriptWidth, int scriptHeight);

} // namespace aegisub_qt
