#pragma once

// libaegisub headers come first: agi::signal (pulled in by ass_file.h and some
// libaegisub headers) has a member named `slots`, which Qt's `#define slots`
// would mangle. This header only uses agi::BackgroundRunner so it stays free of
// both wxWidgets and Qt includes, which also makes it safe to compile into the
// wx-PCH based av_qt_core library.
#include <libaegisub/background_runner.h>

#include <functional>

namespace aegisub_qt {

/// Synchronous BackgroundRunner for the Qt frontend.
///
/// The wxWidgets frontend runs provider setup (video indexing, the libass font
/// index) on a worker thread while a progress dialog is up. M2a does not have a
/// progress dialog, so this implementation simply runs the task inline on the
/// calling thread with a no-op progress sink. It is only used while opening a
/// video or loading subtitles; regular frame decoding does not go through
/// BackgroundRunner.
class QtBackgroundRunner final : public agi::BackgroundRunner {
public:
    void Run(std::function<void(agi::ProgressSink *)> task) override;
};

} // namespace aegisub_qt
