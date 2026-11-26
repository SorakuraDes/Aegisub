#include "export_writer.h"
#include "libaegisub/charset_conv.h"
#include "libaegisub/vfr.h"
#include "automation_job.h"
#include "libaegisub/exception.h"
#include "libaegisub/log.h"
#include "libaegisub/path.h"
#include "options.h"

#include "automation_vm.h"
#include "lua_ass_file.h"
#include "lua_runtime.h"
#include "commit_bridge.h"
#include "qt_path.h"
#include "viewmodels/audio_vm.h"
#include "viewmodels/document_vm.h"
#include "viewmodels/subtitle_grid_model.h"
#include "viewmodels/translation_service.h"
#include "viewmodels/video_vm.h"

#include <QCollator>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QSet>
#include <QThread>
#include <QElapsedTimer>
#include <QTimer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <thread>
#include <exception>
#include <vector>

namespace aegisub_qt::automation {
namespace {
QString pathString(const agi::fs::path &path) {
#ifdef _WIN32
    return QDir::fromNativeSeparators(QString::fromStdWString(path.wstring()));
#else
    return QString::fromStdString(path.string());
#endif
}

QString pathKey(const QString &path) {
    const QFileInfo info(path);
    QString key = info.canonicalFilePath();
    if (key.isEmpty()) key = info.absoluteFilePath();
#ifdef _WIN32
    key = key.toCaseFolded();
#endif
    return key;
}

bool isScript(const QFileInfo &file) {
    const auto suffix = file.suffix().toLower();
    return suffix == QStringLiteral("lua") || suffix == QStringLiteral("moon");
}

void logMessage(int level, const QString &text) {
    if (!agi::log::log) return;
    const int limit = config::opt ? int(OPT_GET("Automation/Trace Level")->GetInt()) : 3;
    if (level <= 1) LOG_W("qt/automation") << text.toStdString();
    else if (level <= limit) LOG_I("qt/automation") << text.toStdString();
}

QString exceptionMessage() {
    try { throw; }
    catch (const agi::Exception &e) { return QString::fromStdString(e.GetMessage()); }
    catch (const std::exception &e) { return QString::fromUtf8(e.what()); }
    catch (...) { return QStringLiteral("Unknown Automation error"); }
}

QString developmentData() {
    // A build-tree executable has no data install beside it. Only recognize an
    // actual source checkout ancestor; never scan an arbitrary working directory.
    QDir dir(QCoreApplication::applicationDirPath());
    do {
        if (dir.exists(QStringLiteral("src/auto4_lua.cpp")) && dir.exists(QStringLiteral("automation/include")))
            return dir.absolutePath();
    } while (dir.cdUp());
    return {};
}

QStringList configuredDirectories(const char *key) {
    QStringList paths;
    if (!config::opt || !config::path) return paths;
    const QString devData = developmentData();
    QSet<QString> seen;
    const auto settings = QString::fromStdString(OPT_GET(key)->GetString()).split(u'|', Qt::SkipEmptyParts);
    for (const auto &token : settings) {
        QString dir = pathString(config::path->Decode(token.toStdString()));
        if (token.startsWith(QStringLiteral("?data/")) && !QFileInfo(dir).isDir() && !devData.isEmpty())
            dir = QDir(devData).filePath(token.mid(6));
        if (!QDir::isAbsolutePath(dir) || !QFileInfo(dir).isDir()) continue;
        const auto key = pathKey(dir);
        if (seen.contains(key)) continue;
        seen.insert(key);
        paths.push_back(QDir::cleanPath(dir));
    }
    return paths;
}

struct MenuNode {
    QString title;
    QString description;
    int id = 0;
    bool enabled = false;
    bool checked = false;
    std::vector<MenuNode> children;
};

QVariantList menuItems(std::vector<MenuNode> &nodes, const QCollator &collator) {
    std::stable_sort(nodes.begin(), nodes.end(), [&](const auto &a, const auto &b) {
        return collator.compare(a.title, b.title) < 0;
    });
    QVariantList items;
    for (auto &node : nodes) {
        items.push_back(QVariantMap{{QStringLiteral("title"), node.title},
                                   {QStringLiteral("description"), node.description},
                                   {QStringLiteral("id"), node.id},
                                   {QStringLiteral("enabled"), node.enabled},
                                   {QStringLiteral("checked"), node.checked},
                                   {QStringLiteral("children"), menuItems(node.children, collator)}});
    }
    return items;
}
} // namespace

struct AutomationViewModel::Impl {
    struct Entry {
        QString path;
        QString key;
        bool autoload;
        std::unique_ptr<LuaScript> script;
        QString error;
        QVector<int> ids;
        QVector<int> filterIds;
    };

    AssFile &file;
    SubtitleGridModel &grid;
    DocumentViewModel &document;
    VideoViewModel &video;
    AudioViewModel &audio;
    TranslationService &i18n;
    std::vector<Entry> entries;
    QVariantList menu;
    bool busy = false;
    int nextId = 1;
    bool menuDirty = true;
    QHash<int, QString> validationErrors;
    agi::signal::Connection commitConnection;
    quint64 revision = 0;
    QTimer pollTimer{nullptr};
    QElapsedTimer elapsed;
    std::shared_ptr<AutomationJob> job;
    std::thread worker;
    std::function<bool()> finish;
    std::optional<AutomationJob::Kind> dialogKind;
    bool visible = false;
    QString shownTitle, shownTask, shownLog;
    double shownProgress = 0;
    QHash<int, QVariantMap> exportOptions;

    void start(const QString &title, std::function<void()> work, std::function<bool()> completion) {
        shownTitle = title;
        shownTask.clear();
        shownLog.clear();
        shownProgress = 0;
        visible = false;
        job->title = title;
        finish = std::move(completion);
        worker = std::thread([state = job, work = std::move(work)] {
            work();
            state->done.store(true);
        });
        elapsed.start();
        pollTimer.start();
    }

    agi::Path projectPaths() const {
        agi::Path paths = *config::path;
        paths.SetToken("?script", ToNativePath(document.filePath()));
        paths.SetToken("?video", ToNativePath(video.filePath()));
        paths.SetToken("?audio", ToNativePath(audio.filePath()));
        return paths;
    }

    QVariantMap properties() const {
        const auto paths = projectPaths();
        const auto &p = file.Properties;
        auto absolute = [&](const std::string &value) {
            return value.empty() ? QString{} : pathString(paths.MakeAbsolute(paths.Decode(value), "?script"));
        };
        return {{QStringLiteral("automation_scripts"), QString::fromStdString(p.automation_scripts)},
                {QStringLiteral("export_filters"), QString::fromStdString(p.export_filters)},
                {QStringLiteral("export_encoding"), QString::fromStdString(p.export_encoding)},
                {QStringLiteral("style_storage"), QString::fromStdString(p.style_storage)},
                {QStringLiteral("video_zoom"), p.video_zoom}, {QStringLiteral("ar_value"), p.ar_value},
                {QStringLiteral("scroll_position"), p.scroll_position}, {QStringLiteral("active_row"), p.active_row},
                {QStringLiteral("ar_mode"), p.ar_mode}, {QStringLiteral("video_position"), p.video_position},
                {QStringLiteral("audio_file"), absolute(p.audio_file)}, {QStringLiteral("video_file"), absolute(p.video_file)},
                {QStringLiteral("timecodes_file"), absolute(p.timecodes_file)}, {QStringLiteral("keyframes_file"), absolute(p.keyframes_file)}};
    }

    Host host(const QString &scriptPath) {
        Host h;
        h.translate = [this](const QString &msg) { return i18n.t(msg); };
        h.log = [scriptPath](int level, const QString &msg) { logMessage(level, QFileInfo(scriptPath).fileName() + QStringLiteral(": ") + msg); };
        h.frameFromMs = [this](int ms) -> std::optional<int> {
            if (video.hasVideo()) return video.frameAtTimeMs(ms, false);
            return std::nullopt;
        };
        h.msFromFrame = [this](int frame) -> std::optional<int> {
            if (video.hasVideo()) return video.timeAtFrameMs(frame, false);
            return std::nullopt;
        };
        h.videoSize = [this]() -> std::optional<VideoSize> {
            if (!video.hasVideo()) return std::nullopt;
            return VideoSize{video.videoWidth(), video.videoHeight(), video.sourceDisplayAspectRatio(), 0};
        };
        h.keyframes = [this]() -> std::optional<QVector<int>> {
            const auto &frames = video.sourceKeyframes();
            return QVector<int>(frames.begin(), frames.end());
        };
        h.properties = [this] { return properties(); };
        h.decodePath = [this](const QString &path) { return pathString(projectPaths().Decode(path.toStdString())); };
        h.fileName = [this] { return QFileInfo(document.filePath()).fileName(); };
        h.progressTask = [log = h.log](const QString &msg) { log(3, msg); };
        h.progressTitle = h.progressTask;
        return h;
    }

    Host workerHost(const QString &scriptPath) {
        Host h;
        const auto state = job;
        const auto fps = video.framerateSnapshot();
        const auto live = host(scriptPath);
        const auto frames = live.keyframes();
        const auto size = live.videoSize();
        const auto props = properties();
        const auto paths = projectPaths();
        const auto filename = live.fileName();
        h.frameFromMs = [fps](int ms) -> std::optional<int> {
            if (!fps.IsLoaded()) return std::nullopt;
            return fps.FrameAtTime(ms, agi::vfr::START);
        };
        h.msFromFrame = [fps](int frame) -> std::optional<int> {
            if (!fps.IsLoaded()) return std::nullopt;
            return fps.TimeAtFrame(frame, agi::vfr::START);
        };
        h.videoSize = [size] { return size; };
        h.keyframes = [frames] { return frames; };
        h.properties = [props] { return props; };
        h.decodePath = [paths](const QString &path) { return pathString(paths.Decode(path.toStdString())); };
        h.fileName = [filename] { return filename; };
        h.onGui = [state](std::function<void()> action) {
            state->ask({AutomationJob::Kind::Gui, {}, std::move(action)});
        };
        h.translate = [state, translate = live.translate](const QString &msg) {
            auto result = std::make_shared<QString>(msg);
            const auto reply = state->ask({AutomationJob::Kind::Gui, {}, [result, msg, translate] { *result = translate(msg); }});
            return reply.contains(QStringLiteral("ok")) ? *result : msg;
        };
        h.isCancelled = [state] { return state->cancelled.load(); };
        h.progressSet = [state](double value) {
            std::lock_guard lock(state->mutex);
            if (std::isfinite(value)) state->progress = std::clamp(value, 0.0, 100.0);
        };
        h.progressTask = [state](const QString &msg) { std::lock_guard lock(state->mutex); state->task = msg; };
        h.progressTitle = [state](const QString &msg) { std::lock_guard lock(state->mutex); state->title = msg; };
        const int trace = config::opt ? int(OPT_GET("Automation/Trace Level")->GetInt()) : 3;
        const QString prefix = QFileInfo(scriptPath).fileName();
        h.log = [state, trace, prefix](int level, const QString &msg) {
            if (level > trace) return;
            std::lock_guard lock(state->mutex);
            if (state->log.size() == 200) state->log.pop_front();
            state->log.push_back({level, (prefix.isEmpty() ? msg : prefix + QStringLiteral(": ") + msg).left(2048)});
        };
        h.allowDialogs = true;
        h.dialog = [state](const QVariantMap &model) { return state->ask({AutomationJob::Kind::Form, model, {}}); };
        h.chooseFile = [state](const QVariantMap &model) {
            return state->ask({AutomationJob::Kind::File, model, {}}).value(QStringLiteral("paths")).toStringList();
        };
        return h;
    }

    void pruneStaleExportOptions() {
        QSet<int> live;
        for (const auto &entry : entries)
            for (int id : entry.filterIds) live.insert(id);
        for (auto it = exportOptions.begin(); it != exportOptions.end();)
            if (live.contains(it.key())) ++it;
            else it = exportOptions.erase(it);
    }

    void load(Entry &entry) {
        for (int id : entry.ids) validationErrors.remove(id);
        entry.ids.clear();
        entry.filterIds.clear();
        entry.error.clear();
        entry.script = std::make_unique<LuaScript>();
        try {
            if (entry.script->load(entry.path, configuredDirectories("Path/Automation/Include"), host(entry.path), &entry.error)) {
                for (qsizetype i = 0; i < entry.script->macros().size(); ++i) entry.ids.push_back(nextId++);
                for (qsizetype i = 0; i < entry.script->filters().size(); ++i) entry.filterIds.push_back(nextId++);
                return;
            }
        } catch (...) {
            entry.error = exceptionMessage();
        }
        if (entry.error.isEmpty()) entry.error = QStringLiteral("Could not load Automation script");
        logMessage(1, entry.path + QStringLiteral(": ") + entry.error);
    }

    void append(const QString &path, bool autoload) {
        const QString key = pathKey(path);
        if (std::any_of(entries.begin(), entries.end(), [&](const Entry &e) { return e.key == key; })) return;
        Entry entry{path, key, autoload, {}, {}, {}};
        load(entry);
        if (autoload) entries.push_back(std::move(entry));
        else entries.insert(std::find_if(entries.begin(), entries.end(), [](const Entry &e) { return e.autoload; }), std::move(entry));
    }

    QVector<int> selectedIndexes(int offset) const {
        auto rows = grid.sortedSelectedRows();
        for (int &row : rows) row += offset + 1;
        return rows;
    }
};

AutomationViewModel::AutomationViewModel(AssFile &file, SubtitleGridModel &grid, DocumentViewModel &document,
                                         VideoViewModel &video, AudioViewModel &audio, TranslationService &i18n)
: impl_(new Impl{file, grid, document, video, audio, i18n}) {
    auto invalidate = [this] { impl_->menuDirty = true; };
    auto invalidateDocument = [this, invalidate] { ++impl_->revision; invalidate(); };
    impl_->commitConnection = file.AddCommitListener([invalidateDocument](int, const AssDialogue *) { invalidateDocument(); });
    connect(&grid, &SubtitleGridModel::selectionCountChanged, this, invalidate);
    connect(&grid, &SubtitleGridModel::activeRowChanged, this, invalidate);
    connect(&grid, &QAbstractItemModel::modelReset, this, invalidateDocument);
    impl_->pollTimer.setInterval(16);
    connect(&impl_->pollTimer, &QTimer::timeout, this, &AutomationViewModel::pollOperation);
    connect(&document, &DocumentViewModel::filePathChanged, this, invalidate);
    connect(&video, &VideoViewModel::videoInfoChanged, this, invalidate);
    connect(&audio, &AudioViewModel::audioChanged, this, invalidate);
    connect(&i18n, &TranslationService::languageChanged, this, [this] {
        if (impl_->busy) return;
        {
            QScopedValueRollback guard(impl_->busy, true);
            for (auto &entry : impl_->entries) impl_->load(entry);
        }
        refresh();
    });
}

AutomationViewModel::~AutomationViewModel() {
    if (impl_->job) impl_->job->cancel();
    if (impl_->worker.joinable()) impl_->worker.join();
}

bool AutomationViewModel::busy() const { return bool(impl_->job); }
bool AutomationViewModel::progressVisible() const { return impl_->visible; }
bool AutomationViewModel::cancelling() const { return impl_->job && impl_->job->cancelled.load(); }
QString AutomationViewModel::progressTitle() const { return impl_->shownTitle; }
QString AutomationViewModel::progressTask() const { return impl_->shownTask; }
double AutomationViewModel::progressValue() const { return impl_->shownProgress; }
QString AutomationViewModel::progressLog() const { return impl_->shownLog; }

void AutomationViewModel::cancelOperation() {
    if (!impl_->job) return;
    impl_->job->cancel();
    emit operationChanged();
}

void AutomationViewModel::completeDialog(const QVariant &button, const QVariantMap &values) {
    if (impl_->dialogKind != AutomationJob::Kind::Form || !impl_->job) return;
    impl_->dialogKind.reset();
    impl_->job->reply({{QStringLiteral("button"), button}, {QStringLiteral("values"), values}});
}

void AutomationViewModel::completeFile(const QList<QUrl> &urls) {
    if (impl_->dialogKind != AutomationJob::Kind::File || !impl_->job) return;
    impl_->dialogKind.reset();
    QStringList paths;
    for (const auto &url : urls) if (url.isLocalFile()) paths.push_back(QDir::toNativeSeparators(url.toLocalFile()));
    impl_->job->reply({{QStringLiteral("paths"), paths}});
}

bool AutomationViewModel::reportError(const QString &name, const QString &error) {
    const QString detail = name + QStringLiteral(": ") + error;
    logMessage(1, detail);
    emit executionError(detail);
    return false;
}

void AutomationViewModel::pollOperation() {
    const auto job = impl_->job;
    if (!job) return;
    std::optional<AutomationJob::Request> request;
    QList<QPair<int, QString>> newLogs;
    bool dirty = false;
    {
        std::lock_guard lock(job->mutex);
        request = std::exchange(job->pending, std::nullopt);
        newLogs.swap(job->log);
        dirty = impl_->shownTitle != job->title || impl_->shownTask != job->task || impl_->shownProgress != job->progress;
        impl_->shownTitle = job->title;
        impl_->shownTask = job->task;
        impl_->shownProgress = job->progress;
    }
    if (!newLogs.isEmpty()) {
        for (const auto &[level, line] : newLogs) {
            logMessage(level, line);
            impl_->shownLog += line + u'\n';
        }
        impl_->shownLog = impl_->shownLog.right(32768);
        dirty = true;
    }
    if (request && !job->cancelled.load()) {
        if (request->kind == AutomationJob::Kind::Gui) {
            request->gui();
            job->reply({{QStringLiteral("ok"), true}});
        } else {
            impl_->dialogKind = request->kind;
            impl_->visible = false;
            emit operationChanged();
            if (request->kind == AutomationJob::Kind::Form) emit dialogRequested(request->data);
            else {
                auto &data = request->data;
                const auto parts = data.value(QStringLiteral("wildcard")).toString().split(u'|');
                QStringList filters;
                auto pattern = [](QString value) {
                    value.replace(QRegularExpression(QStringLiteral("[;\\s]+")), QStringLiteral(" "));
                    return value.replace(QStringLiteral("*.*"), QStringLiteral("*")).trimmed();
                };
                for (qsizetype i = 1; i < parts.size(); i += 2) {
                    const auto glob = pattern(parts[i]);
                    if (!glob.isEmpty()) filters << parts[i - 1] + QStringLiteral(" (") + glob + u')';
                }
                if (filters.isEmpty() && parts.size() == 1 && !parts.front().trimmed().isEmpty())
                    filters << impl_->i18n.t(QStringLiteral("Files")) + QStringLiteral(" (") + pattern(parts.front()) + u')';
                data.insert(QStringLiteral("filters"), filters);
                emit fileRequested(data);
            }
        }
    }
    if (job->done.load()) {
        impl_->pollTimer.stop();
        impl_->worker.join();
        const auto finish = std::exchange(impl_->finish, {});
        bool success = false;
        try { success = !job->cancelled.load() && finish(); }
        catch (...) { reportError(impl_->shownTitle, exceptionMessage()); }
        impl_->dialogKind.reset();
        impl_->visible = false;
        impl_->job.reset();
        impl_->busy = false;
        emit operationChanged();
        emit operationFinished(success);
        return;
    }
    const bool visible = !impl_->dialogKind && impl_->elapsed.elapsed() >= 180;
    if (visible != impl_->visible || dirty) {
        impl_->visible = visible;
        emit operationChanged();
    }
}
QVariantList AutomationViewModel::menu() const { return impl_->menu; }

QVariantList AutomationViewModel::scripts() const {
    QVariantList list;
    for (const auto &entry : impl_->entries) {
        const auto &data = entry.script->metadata();
        const auto fileName = QFileInfo(entry.path).fileName();
        list.push_back(QVariantMap{{QStringLiteral("name"), data.name.isEmpty() ? fileName : data.name},
                                  {QStringLiteral("fileName"), fileName}, {QStringLiteral("path"), entry.path},
                                  {QStringLiteral("description"), data.description}, {QStringLiteral("error"), entry.error},
                                  {QStringLiteral("autoload"), entry.autoload}, {QStringLiteral("author"), data.author},
                                  {QStringLiteral("version"), data.version}});
    }
    return list;
}

QUrl AutomationViewModel::lastFolder() const {
    if (!config::opt || !config::path) return {};
    try {
        const auto dir = pathString(config::path->Decode(OPT_GET("Path/Last/Automation")->GetString()));
        if (QDir::isAbsolutePath(dir)) return QUrl::fromLocalFile(dir);
    } catch (...) { logMessage(1, exceptionMessage()); }
    return {};
}

void AutomationViewModel::refresh() {
    impl_->pruneStaleExportOptions();
    impl_->menuDirty = true;
    rebuildMenu(false);
    emit scriptsChanged();
    emit exportFiltersChanged();
}

void AutomationViewModel::reloadAutoload() {
    if (impl_->busy) return;
    {
        QScopedValueRollback guard(impl_->busy, true);
        auto &entries = impl_->entries;
        entries.erase(std::remove_if(entries.begin(), entries.end(), [this](const Impl::Entry &e) {
            if (!e.autoload) return false;
            for (int id : e.ids) impl_->validationErrors.remove(id);
            return true;
        }), entries.end());
        try {
            for (const auto &path : configuredDirectories("Path/Automation/Autoload")) {
                const auto files = QDir(path).entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase);
                for (const auto &file : files)
                    if (isScript(file)) impl_->append(file.absoluteFilePath(), true);
            }
        } catch (...) { logMessage(1, exceptionMessage()); }
    }
    refresh();
}

void AutomationViewModel::addScripts(const QList<QUrl> &urls) {
    if (impl_->busy) return;
    {
        QScopedValueRollback guard(impl_->busy, true);
        for (const auto &url : urls) {
            if (!url.isLocalFile()) continue;
            const QFileInfo file(url.toLocalFile());
            if (!file.isFile() || !isScript(file)) continue;
            try {
                impl_->append(file.absoluteFilePath(), false);
                if (config::opt) OPT_SET("Path/Last/Automation")->SetString(file.absolutePath().toStdString());
            } catch (...) { logMessage(1, exceptionMessage()); }
        }
    }
    refresh();
}

void AutomationViewModel::removeScript(int index) {
    auto &entries = impl_->entries;
    if (impl_->busy || index < 0 || size_t(index) >= entries.size() || entries[index].autoload) return;
    for (int id : entries[index].ids) impl_->validationErrors.remove(id);
    entries.erase(entries.begin() + index);
    refresh();
}

void AutomationViewModel::reloadScript(int index) {
    auto &entries = impl_->entries;
    if (impl_->busy || index < 0 || size_t(index) >= entries.size()) return;
    {
        QScopedValueRollback guard(impl_->busy, true);
        impl_->load(entries[index]);
    }
    refresh();
}

QString AutomationViewModel::scriptInfo(int index) const {
    const auto &entries = impl_->entries;
    if (index < 0 || size_t(index) >= entries.size()) {
        const auto globals = std::count_if(entries.begin(), entries.end(), [](const Impl::Entry &e) { return e.autoload; });
        return QStringLiteral("LuaJIT / MoonScript (*.lua, *.moon)\n%1: %2\n%3: %4")
            .arg(impl_->i18n.t(QStringLiteral("Global scripts"))).arg(globals)
            .arg(impl_->i18n.t(QStringLiteral("Local scripts"))).arg(entries.size() - globals);
    }
    const auto &entry = entries[index];
    const auto &meta = entry.script->metadata();
    QStringList lines{meta.name, entry.path, meta.description, meta.author, meta.version};
    if (!entry.error.isEmpty()) lines << entry.error;
    else {
        for (const auto &m : entry.script->macros()) lines << QStringLiteral("Macro: %1").arg(m.name);
        for (const auto &f : entry.script->filters()) lines << QStringLiteral("Filter: %1 (%2)").arg(f.name).arg(f.priority);
    }
    lines.removeAll(QString{});
    return lines.join(u'\n');
}

void AutomationViewModel::validateMenu() {
    if (impl_->busy || !impl_->menuDirty) return;
    impl_->menuDirty = false;
    rebuildMenu(true);
}

void AutomationViewModel::rebuildMenu(bool validate) {
    QVariantList updated;
    try {
        QScopedValueRollback guard(impl_->busy, true);
        std::vector<MenuNode> tree;
        std::unique_ptr<LuaAssFile> snapshot;
        for (auto &entry : impl_->entries) {
            for (qsizetype i = 0; i < entry.ids.size(); ++i) {
                const auto &macro = entry.script->macros()[i];
                const int id = entry.ids[i];
                Outcome outcome;
                outcome.enabled = true;
                if (validate && (macro.hasValidate || macro.hasIsActive)) {
                    if (!snapshot) snapshot = std::make_unique<LuaAssFile>(impl_->file, false);
                    const int offset = snapshot->initialEventOffset();
                    const int row = impl_->grid.activeRow();
                    outcome = entry.script->validate(int(i), *snapshot, impl_->selectedIndexes(offset), row < 0 ? 0 : row + offset + 1);
                    if (!outcome.error.isEmpty() && impl_->validationErrors.value(id) != outcome.error) {
                        impl_->validationErrors.insert(id, outcome.error);
                        logMessage(1, macro.name + QStringLiteral(": ") + outcome.error);
                    } else if (outcome.error.isEmpty()) impl_->validationErrors.remove(id);
                }
                auto parts = macro.name.split(u'/', Qt::SkipEmptyParts);
                if (parts.isEmpty()) parts.push_back(macro.name);
                if (!outcome.title.isEmpty()) parts.back() = outcome.title;
                auto *level = &tree;
                for (qsizetype j = 0; j < parts.size() - 1; ++j) {
                    const auto &part = parts[j];
                    auto found = std::find_if(level->begin(), level->end(), [&](const MenuNode &node) { return !node.id && node.title == part; });
                    if (found == level->end()) {
                        level->push_back({part, {}, 0, true, false, {}});
                        found = std::prev(level->end());
                    }
                    level = &found->children;
                }
                level->push_back({parts.back(), outcome.description.isEmpty() ? macro.description : outcome.description,
                                  id, outcome.enabled && outcome.error.isEmpty() && !outcome.cancelled, outcome.checked, {}});
            }
        }
        const QCollator collator(QLocale(impl_->i18n.language()));
        updated = menuItems(tree, collator);
    } catch (...) { logMessage(1, exceptionMessage()); }
    if (updated != impl_->menu) {
        impl_->menu = std::move(updated);
        emit menuChanged();
    }
}

bool AutomationViewModel::runMacro(int id) {
    if (impl_->busy || QThread::currentThread() != thread() || id <= 0) return false;
    auto fail = [this](const QString &name, const QString &message) { return reportError(name, message); };
    for (auto &entry : impl_->entries) {
        const auto index = entry.ids.indexOf(id);
        if (index < 0) continue;
        QScopedValueRollback guard(impl_->busy, true);
        // A direct run may also change script-local state used by validators.
        impl_->menuDirty = true;
        const auto &macro = entry.script->macros()[index];
        try {
            const int activeRow = impl_->grid.activeRow();
            QList<int> rows = impl_->grid.sortedSelectedRows();
            const int offset = int(impl_->file.Info.size() + impl_->file.Styles.size());
            const int active = activeRow < 0 ? 0 : activeRow + offset + 1;
            const auto selected = impl_->selectedIndexes(offset);
            if (macro.hasValidate) {
                LuaAssFile readOnly(impl_->file, false);
                const auto validation = entry.script->validate(int(index), readOnly, selected, active);
                if (!validation.error.isEmpty()) return fail(macro.name, validation.error);
                if (!validation.enabled || validation.cancelled) return false;
            }
            const auto snapshotOwner = std::make_shared<LuaAssFile>(impl_->file, true);
            const auto resultOwner = std::make_shared<Outcome>();
            impl_->job = std::make_shared<AutomationJob>();
            const auto host = impl_->workerHost(entry.path);
            auto *script = entry.script.get();
            const auto name = macro.name;
            const auto revision = impl_->revision;
            impl_->start(name, [script, index, snapshotOwner, selected, active, host, resultOwner] {
                try { *resultOwner = script->run(int(index), *snapshotOwner, selected, active, &host); }
                catch (...) { resultOwner->error = exceptionMessage(); }
            }, [this, name, revision, snapshotOwner, resultOwner, activeRow, active, rows]() mutable {
            auto &snapshot = *snapshotOwner;
            const auto &result = *resultOwner;
            if (result.cancelled) return false;
            if (!result.error.isEmpty()) return reportError(name, result.error);
            if (revision != impl_->revision)
                return reportError(name, impl_->i18n.t(QStringLiteral("Subtitles changed while Automation was running.")));
            int target = activeRow;
            if (result.selection) {
                rows.clear();
                for (const int absolute : *result.selection) {
                    const int row = snapshot.eventOrdinal(absolute);
                    if (row >= 0) rows.push_back(row);
                }
                target = snapshot.eventOrdinal(result.active.value_or(active));
                if (!rows.isEmpty() && !rows.contains(target)) target = *std::min_element(rows.begin(), rows.end());
                if (target < 0) target = activeRow;
            } else if (result.active) {
                target = snapshot.eventOrdinal(*result.active);
            }
            const auto label = (result.undoLabel.isEmpty() ? name : result.undoLabel).toStdString();
            const int flags = snapshot.publish(impl_->file);
            if (flags) commit(&impl_->file, label, flags, -1, nullptr);
            if (flags || result.selection || result.active) impl_->grid.setAutomationSelection(rows, target);
            return true;
            });
            guard.commit();
            emit operationChanged();
            return true;
        } catch (...) {
            impl_->job.reset();
            impl_->finish = {};
            return fail(macro.name, exceptionMessage());
        }
    }
    return false;
}

QStringList AutomationViewModel::exportEncodings() const {
    QStringList values;
    for (const auto &name : agi::charset::GetEncodingsList<std::vector<std::string>>())
        values << QString::fromStdString(name);
    return values;
}

QString AutomationViewModel::exportEncoding() const {
    const auto value = QString::fromStdString(impl_->file.Properties.export_encoding);
    return exportEncodings().contains(value) ? value : QStringLiteral("Unicode (UTF-8)");
}

QVariantList AutomationViewModel::exportFilters() const {
    const auto selected = QString::fromStdString(impl_->file.Properties.export_filters).split(u'|');
    std::vector<QVariantMap> rows;
    QSet<QString> names;
    auto add = [&](int id, QString name, const QString &description, int priority, bool config) {
        const QString base = name;
        for (int suffix = 1; names.contains(name); ++suffix)
            name = base + QStringLiteral(" (%1)").arg(suffix);
        names.insert(name);
        rows.push_back({{QStringLiteral("id"), id}, {QStringLiteral("name"), name},
                        {QStringLiteral("description"), description}, {QStringLiteral("priority"), priority},
                        {QStringLiteral("hasConfig"), config}, {QStringLiteral("checked"), selected.contains(name)}});
    };
    add(-1, impl_->i18n.t(QStringLiteral("Fix Styles")),
        impl_->i18n.t(QStringLiteral("Fixes styles by replacing any style that isn't available on file with Default.")), -5000, false);
    for (const auto &entry : impl_->entries) {
        const auto &filters = entry.script->filters();
        for (qsizetype i = 0; i < entry.filterIds.size(); ++i) {
            const auto &filter = filters[i];
            add(entry.filterIds[i], filter.name, filter.description, filter.priority, filter.hasConfig);
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(QStringLiteral("priority")).toInt() > b.value(QStringLiteral("priority")).toInt();
    });
    QVariantList out;
    for (auto &row : rows) out.push_back(std::move(row));
    return out;
}

bool AutomationViewModel::configureExportFilter(int id) {
    if (impl_->busy || QThread::currentThread() != thread()) return false;
    for (const auto &entry : impl_->entries) {
        const auto index = entry.filterIds.indexOf(id);
        if (index < 0 || !entry.script->filters()[index].hasConfig) continue;
        const auto name = entry.script->filters()[index].name;
        try {
            const auto subs = std::make_shared<LuaAssFile>(impl_->file, false);
            const auto previous = impl_->exportOptions.value(id);
            const auto result = std::make_shared<Outcome>();
            const auto values = std::make_shared<QVariantMap>();
            impl_->job = std::make_shared<AutomationJob>();
            const auto host = impl_->workerHost(entry.path);
            impl_->busy = true;
            impl_->start(name, [script = entry.script.get(), index, subs, previous, result, values, host] {
                try {
                    auto config = script->configureFilter(int(index), *subs, previous, &host);
                    result->error = std::move(config.error);
                    result->cancelled = config.cancelled;
                    if (!result->error.isEmpty() || result->cancelled) return;
                    const auto response = host.dialog(config.dialog);
                    const auto button = response.value(QStringLiteral("button"));
                    result->cancelled = !button.isValid() || button.metaType().id() == QMetaType::Bool;
                    *values = response.value(QStringLiteral("values")).toMap();
                } catch (...) { result->error = exceptionMessage(); }
            }, [this, id, name, result, values] {
                if (result->cancelled) return false;
                if (!result->error.isEmpty()) return reportError(name, result->error);
                impl_->exportOptions.insert(id, *values);
                return true;
            });
            emit operationChanged();
            return true;
        } catch (...) {
            impl_->job.reset();
            impl_->finish = {};
            impl_->busy = false;
            return reportError(name, exceptionMessage());
        }
    }
    return false;
}

bool AutomationViewModel::exportSubtitles(const QUrl &url, const QString &encoding, const QVariantList &orderedIds) {
    if (impl_->busy || QThread::currentThread() != thread() || !url.isLocalFile()) return false;
    const auto name = impl_->i18n.t(QStringLiteral("Export Subtitles"));
    try {
        if (!exportEncodings().contains(encoding)) return reportError(name, QStringLiteral("Unknown text encoding"));
        if (!impl_->document.filePath().isEmpty() && pathKey(url.toLocalFile()) == pathKey(impl_->document.filePath()))
            return reportError(name, impl_->i18n.t(QStringLiteral("Choose a different file for export.")));
        const auto path = url.toLocalFile();
        const auto suffix = QFileInfo(path).suffix().toLower();
        if (suffix != QStringLiteral("ass") && suffix != QStringLiteral("srt"))
            return reportError(name, QStringLiteral("Unknown subtitle export format"));
        const auto subs = std::make_shared<AssFile>(impl_->file);
        const auto result = std::make_shared<Outcome>();
        impl_->job = std::make_shared<AutomationJob>();
        const auto state = impl_->job;
        struct Step { LuaScript *script; int index; QVariantMap options; Host host; QString name; };
        std::vector<Step> steps;
        QSet<int> seen;
        for (const auto &value : orderedIds) {
            const int id = value.toInt();
            if (seen.contains(id)) continue;
            seen.insert(id);
            if (id == -1) {
                steps.push_back({nullptr, 0, {}, {}, impl_->i18n.t(QStringLiteral("Fix Styles"))});
                continue;
            }
            bool found = false;
            for (const auto &entry : impl_->entries) {
                const auto index = entry.filterIds.indexOf(id);
                if (index < 0) continue;
                steps.push_back({entry.script.get(), int(index), impl_->exportOptions.value(id),
                                 impl_->workerHost(entry.path), entry.script->filters()[index].name});
                found = true;
                break;
            }
            if (!found) throw std::runtime_error("An export filter is no longer available");
        }
        const auto writing = impl_->i18n.t(QStringLiteral("Writing subtitles"));
        impl_->busy = true;
        impl_->start(name, [path, suffix, subs, encoding, steps, result, state, writing] {
            try {
                for (const auto &step : steps) {
                    if (state->cancelled.load()) return;
                    { std::lock_guard lock(state->mutex); state->task = step.name; }
                    if (!step.script) {
                        QSet<QString> styles;
                        for (const auto &style : subs->GetStyles()) styles.insert(QString::fromStdString(style).toCaseFolded());
                        for (auto &line : subs->Events)
                            if (!styles.contains(QString::fromStdString(line.Style.get()).toCaseFolded())) line.Style = "Default";
                        continue;
                    }
                    auto options = step.options;
                    if (step.script->filters()[step.index].hasConfig && options.isEmpty()) {
                        LuaAssFile configSubs(*subs, false);
                        const auto config = step.script->configureFilter(step.index, configSubs, {}, &step.host);
                        if (!config.error.isEmpty() || config.cancelled) {
                            result->error = config.error; result->cancelled = config.cancelled; return;
                        }
                        for (const auto &item : config.dialog.value(QStringLiteral("controls")).toList()) {
                            const auto control = item.toMap();
                            if (control.value(QStringLiteral("class")).toString() != QStringLiteral("label"))
                                options.insert(control.value(QStringLiteral("name")).toString(), control.value(QStringLiteral("value")));
                        }
                    }
                    LuaAssFile snapshot(*subs, true);
                    *result = step.script->runFilter(step.index, snapshot, options, &step.host);
                    if (!result->error.isEmpty() || result->cancelled || state->cancelled.load()) return;
                    snapshot.publish(*subs);
                }
                if (state->cancelled.load()) return;
                { std::lock_guard lock(state->mutex); state->task = writing; }
                QTemporaryDir temporary;
                if (!temporary.isValid()) throw std::runtime_error(temporary.errorString().toStdString());
                const auto temporaryPath = temporary.filePath(QStringLiteral("subtitles.") + suffix);
                if (!writeExportFile(*subs, temporaryPath, encoding, state->cancelled) || state->cancelled.load()) return;
                QFile source(temporaryPath);
                if (!source.open(QIODevice::ReadOnly)) throw std::runtime_error(source.errorString().toStdString());
                QSaveFile output(path);
                output.setDirectWriteFallback(false);
                if (!output.open(QIODevice::WriteOnly))
                    throw std::runtime_error(QStringLiteral("Cannot open export target %1: %2").arg(path, output.errorString()).toStdString());
                while (!source.atEnd() && !state->cancelled.load()) {
                    const auto chunk = source.read(256 * 1024);
                    if (chunk.isEmpty() && source.error() != QFileDevice::NoError)
                        throw std::runtime_error(source.errorString().toStdString());
                    if (output.write(chunk) != chunk.size())
                        throw std::runtime_error(QStringLiteral("Cannot write export target %1: %2").arg(path, output.errorString()).toStdString());
                }
                if (state->cancelled.load()) return;
                if (!output.commit())
                    throw std::runtime_error(QStringLiteral("Cannot replace export target %1: %2").arg(path, output.errorString()).toStdString());
            } catch (...) { result->error = exceptionMessage(); }
        }, [this, name, result] {
            if (result->cancelled) return false;
            if (!result->error.isEmpty()) return reportError(name, result->error);
            return true;
        });
        emit operationChanged();
        return true;
    } catch (...) {
        impl_->job.reset();
        impl_->finish = {};
        impl_->busy = false;
        return reportError(name, exceptionMessage());
    }
}

} // namespace aegisub_qt::automation
