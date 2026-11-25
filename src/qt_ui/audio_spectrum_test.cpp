#include "audio_bridge.h"
#include "aegisub_core.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
using aegisub_qt::AudioBridge;
constexpr int bins = AudioBridge::spectrumBins;

void require(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

void testSpectrum() {
    AudioBridge bridge;
    std::string error;
    if (!bridge.OpenDummy(1, &error)) throw std::runtime_error("OpenDummy: " + error);
    const int rate = bridge.SampleRate();
    const auto count = bridge.SampleCount();
    require(rate > 0 && count == rate && bridge.DecodedSamples() == count,
            "expected one decoded second of dummy audio");

    std::vector<float> levels;
    auto compute = [&](int64_t start, int64_t end, int width) {
        levels.assign(1, -1.0f); // Invalid requests must replace previous output too.
        bridge.ComputeSpectrum(start, end, width, levels);
        require(levels.size() == static_cast<std::size_t>(std::max(0, width)) * bins,
                "unexpected spectrum size");
        require(std::all_of(levels.begin(), levels.end(), [](float v) { return v >= 0 && v <= 1; }),
                "spectrum contains a nonfinite or unnormalized level");
    };
    auto peak = [&](int col) {
        const auto first = levels.cbegin() + col * bins;
        return std::max_element(first, first + bins);
    };

    constexpr int columns = 5;
    compute(count / 4, 3 * count / 4, columns);
    const int expected = static_cast<int>(std::lround(440.0 * (2 * (bins - 1)) / rate));
    for (int x = 0; x < columns; ++x) {
        const auto strongest = peak(x);
        const auto bin = strongest - (levels.cbegin() + x * bins);
        require(bin == expected, "440 Hz peak in column " + std::to_string(x) +
                ": got bin " + std::to_string(bin) + ", expected " + std::to_string(expected));
        // The dummy tone peaks around -10 dBFS in this part of its envelope.
        require(*strongest > 0.8f && *strongest < 0.95f, "unexpected normalized 440 Hz level");
    }

    compute(-1, 1, 2); // Column centers: -1 and the first valid sample.
    require(*peak(0) == 0 && *peak(1) > 0, "incorrect spectrum at first sample");
    compute(count - 1, count + 1, 2);
    require(*peak(0) > 0 && *peak(1) == 0, "incorrect spectrum at last sample or EOF");
    compute(1, 1, 1);
    require(*peak(0) == 0, "empty interval must be silent");
    compute(2, 1, 1);
    require(*peak(0) == 0, "reversed interval must be silent");
    compute(0, count, 0);
    compute(0, count, -1);
    bridge.Close();
    compute(0, count, 2);
    require(*peak(0) == 0 && *peak(1) == 0, "closed provider must be silent");
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    // Prefer the build directory: some Windows installations deny Qt's secure
    // temporary-directory creation under the user's global TEMP folder.
    QTemporaryDir config(QCoreApplication::applicationDirPath() + "/spectrum-test-XXXXXX");
    try {
        require(config.isValid() && qputenv("AEGISUB_QT_CONFIG_DIR", config.path().toLocal8Bit()),
                "cannot isolate the test configuration");
        QString error;
        if (!aegisub_qt::InitializeCore(&error)) throw std::runtime_error(error.toStdString());
        testSpectrum();
        return EXIT_SUCCESS;
    } catch (const std::exception &e) {
        std::cerr << "audio spectrum: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
