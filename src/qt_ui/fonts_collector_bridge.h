#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class AssFile;

namespace aegisub_qt {

struct FontsCollectorResult {
    std::vector<std::filesystem::path> paths;
    int missingCount = 0;
    bool outputFailed = false;
};

// Actions: 0 checks only; 1 and 2 copy to the supplied directory; 3 writes a ZIP.
// The callback receives UTF-8 log fragments synchronously on the calling thread.
FontsCollectorResult CollectFonts(AssFile const& snapshot, int action,
    std::filesystem::path const& destination, std::function<void(std::string)> const& log);

} // namespace aegisub_qt
