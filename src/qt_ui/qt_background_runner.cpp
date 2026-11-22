#include "qt_background_runner.h"

#include <libaegisub/log.h>

#include <cstdint>
#include <string>

namespace aegisub_qt {
namespace {

/// No-op progress sink: provider setup is synchronous here and there is no
/// progress UI to report to.
class QtProgressSink final : public agi::ProgressSink {
public:
    void SetIndeterminate() override {}
    void SetTitle(std::string const &) override {}
    void SetMessage(std::string const &) override {}
    void SetProgress(int64_t, int64_t) override {}
    void Log(std::string const &message) override { LOG_I("qt/background") << message; }
    void SetStayOpen(bool) override {}
    bool IsCancelled() override { return false; }
};

} // namespace

void QtBackgroundRunner::Run(std::function<void(agi::ProgressSink *)> task) {
    QtProgressSink sink;
    task(&sink);
}

} // namespace aegisub_qt
