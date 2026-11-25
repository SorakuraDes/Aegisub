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
// Read the first valid top-level move. Untimed moves report zero times (ms).
// On failure, outputs are unchanged. All output pointers may be null.
bool parseMove(std::string_view text, double* x1, double* y1, double* x2, double* y2,
               int* t1 = nullptr, int* t2 = nullptr);
bool parseOrg(std::string_view text, double* x, double* y);
// Prefer frz over the legacy fr alias, and fall back to the style or 0 degrees.
// Return whether an override or a style supplied the value.
bool parseRotationZ(std::string_view text, const AssStyle* style, double* angleDeg);
// Use the style or 100% for each missing axis; return whether any override or style was found.
bool parseScale(std::string_view text, const AssStyle* style, double* x, double* y);
// Read the last valid top-level rectangular clip, ignoring vectors and iclip.
bool parseRectClip(std::string_view text, int* x1, int* y1, int* x2, int* y2);

// Set a static position, removing competing top-level pos/move tags.
// Nonfinite coordinates leave the text unchanged.
std::string setPosTag(std::string_view text, double x, double y);
// Update an existing move or convert the first position in place, removing competitors.
// The first valid existing move supplies its original optional times.
std::string setMoveTag(std::string_view text, double x1, double y1, double x2, double y2);
// Normalize to [-180, 180] degrees and write a canonical frz tag.
std::string setRotationZTag(std::string_view text, double angleDeg);
// Clamp both scale percentages to at least 1.
std::string setScaleTags(std::string_view text, double x, double y);
// Replace existing ordinary rectangles; never edit vector or inverse clips.
// Do not add a rectangle when only a vector or inverse clip is present.
std::string setRectClipTag(std::string_view text, int x1, int y1, int x2, int y2);
std::string removeRectClipTag(std::string_view text);

// Scale positions, moves, origins, rectangular clips, and absolute font sizes
// in place, including tags in transforms. Vector clips and move times are kept.
std::string resampleLineTags(std::string_view text, double scaleX, double scaleY);

// wx visual-tool anchor: explicit pos, then move start, then alignment/margins.
std::pair<double, double> defaultAnchorPos(const AssDialogue& line, const AssStyle* style,
                                           int scriptWidth, int scriptHeight);

} // namespace aegisub_qt
