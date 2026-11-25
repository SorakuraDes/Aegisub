#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <functional>
#include <memory>
#include <optional>

namespace aegisub_qt::automation {

class LuaAssFile;

struct VideoSize {
    int width = 0;
    int height = 0;
    double aspectRatio = 0.0;
    int aspectRatioType = 0;
};

// Callbacks must be safe to call on the thread owning this script state.
// GUI state should be captured by the caller before invocation.
struct Host {
    std::function<QString(const QString&)> translate;
    std::function<void(int, const QString&)> log;
    std::function<std::optional<int>(int)> frameFromMs;
    std::function<std::optional<int>(int)> msFromFrame;
    std::function<std::optional<VideoSize>()> videoSize;
    std::function<std::optional<QVector<int>>()> keyframes;
    std::function<QVariantMap()> properties;
    std::function<bool()> isCancelled;
    std::function<void(double)> progressSet;
    std::function<void(const QString&)> progressTask;
    std::function<void(const QString&)> progressTitle;
    std::function<QString(const QString&)> decodePath;
    std::function<QString()> fileName;
};

struct ScriptMetadata {
    QString name;
    QString description;
    QString author;
    QString version;
};

struct Macro {
    QString name;
    QString description;
    bool hasValidate = false;
    bool hasIsActive = false;
};

struct Filter {
    QString name;
    QString description;
    int priority = 0;
    bool hasConfig = false;
};

struct Outcome {
    QString error;
    bool cancelled = false;
    QString undoLabel;
    // Absolute, 1-based ASS indexes after the operation. Nil leaves the input
    // selection or active row unchanged; the caller resolves this on commit.
    std::optional<QVector<int>> selection;
    std::optional<int> active;
    bool enabled = true;
    bool checked = false;
    QString description;
    QString title;
};

// One independent Lua state per script. Not concurrently callable.
class LuaScript {
public:
    LuaScript();
    ~LuaScript();
    LuaScript(const LuaScript&) = delete;
    LuaScript& operator=(const LuaScript&) = delete;

    bool load(const QString& filename, const QStringList& includeDirs,
              Host host = {}, QString* error = nullptr);
    const ScriptMetadata& metadata() const;
    const QVector<Macro>& macros() const;
    const QVector<Filter>& filters() const;

    // Macro index is 0-based. ASS inputs are sorted 1-based absolute indexes;
    // the active index is 0 when absent. Validation needs a read-only proxy.
    // A failed/cancelled run must be discarded by the caller, never published.
    Outcome validate(int macro, LuaAssFile& subs, const QVector<int>& selection, int active);
    Outcome run(int macro, LuaAssFile& subs, const QVector<int>& selection, int active);

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace aegisub_qt::automation
