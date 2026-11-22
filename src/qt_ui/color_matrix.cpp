// ColorMatrix helpers extracted from src/video_provider_manager.cpp.
//
// The wxWidgets frontend embeds them in the video provider factory, together
// with wxWidgets track-selection dialogs. The Qt frontend only needs the
// wx-free colour conversion helpers (FFmpegSourceVideoProvider calls them while
// opening a file), so they are copied here instead of linking
// video_provider_manager.cpp. Keep this in sync with the original if its
// guessing rules ever change.

#include "include/aegisub/video_provider.h"

#include <libaegisub/split.h>

#include <string>
#include <utility>
#include <vector>

namespace ColorMatrix {

std::string colormatrix_description(int cs, int cr) {
	// Assuming TV for unspecified
	std::string str = cr == AGI_CR_JPEG ? "PC" : "TV";

	switch (cs) {
		case AGI_CS_RGB:
			return "None";
		case AGI_CS_BT709:
			return str + ".709";
		case AGI_CS_FCC:
			return str + ".FCC";
		case AGI_CS_BT470BG:
		case AGI_CS_SMPTE170M:
			return str + ".601";
		case AGI_CS_SMPTE240M:
			return str + ".240M";
		default:
			return "";
	}
}

std::pair<int, int> parse_colormatrix(std::string matrix) {
	int cs = AGI_CS_UNSPECIFIED;
	int cr = AGI_CR_UNSPECIFIED;

	std::vector<std::string> parts;
	agi::Split(parts, matrix, '.');
	if (parts.size() == 2) {
		if (parts[0] == "TV") {
			cr = AGI_CR_MPEG;
		} else if (parts[0] == "PC") {
			cr = AGI_CR_JPEG;
		}

		if (parts[1] == "709") {
			cs = AGI_CS_BT709;
		} else if (parts[1] == "601") {
			cs = AGI_CS_BT470BG;
		} else if (parts[1] == "FCC") {
			cs = AGI_CS_FCC;
		} else if (parts[1] == "240M") {
			cs = AGI_CS_SMPTE240M;
		}
	}

	return std::make_pair(cs, cr);
}

void guess_colorspace(int &CS, int &CR, int Width, int Height) {
	if (CS == AGI_CS_UNSPECIFIED)
		CS = Width > 1024 || Height >= 600 ? AGI_CS_BT709 : AGI_CS_BT470BG;
	if (CR != AGI_CR_MPEG)
		CR = AGI_CR_MPEG;
}

void override_colormatrix(int &CS, int &CR, std::string matrix, int Width, int Height) {
	guess_colorspace(CS, CR, Width, Height);
	auto [oCS, oCR] = parse_colormatrix(matrix);
	if (oCS != AGI_CS_UNSPECIFIED && oCR != AGI_CR_UNSPECIFIED) {
		CS = oCS;
		CR = oCR;
	}
}

} // namespace ColorMatrix
