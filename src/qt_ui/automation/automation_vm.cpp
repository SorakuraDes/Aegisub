#include "ass_file.h"
#include "ass_info.h"
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

#include <algorithm>
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

    void load(Entry &entry) {
        for (int id : entry.ids) validationErrors.remove(id);
        entry.ids.clear();
        entry.error.clear();
        entry.script = std::make_unique<LuaScript>();
        try {
            if (entry.script->load(entry.path, configuredDirectories("Path/Automation/Include"), host(entry.path), &entry.error)) {
                for (qsizetype i = 0; i < entry.script->macros().size(); ++i) entry.ids.push_back(nextId++);
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
    impl_->commitConnection = file.AddCommitListener([invalidate](int, const AssDialogue *) { invalidate(); });
    connect(&grid, &SubtitleGridModel::selectionCountChanged, this, invalidate);
    connect(&grid, &SubtitleGridModel::activeRowChanged, this, invalidate);
    connect(&grid, &QAbstractItemModel::modelReset, this, invalidate);
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

AutomationViewModel::~AutomationViewModel() = default;
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
    impl_->menuDirty = true;
    rebuildMenu(false);
    emit scriptsChanged();
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
    auto fail = [this](const QString &name, const QString &message) {
        const QString detail = name + QStringLiteral(": ") + message;
        logMessage(1, detail);
        emit executionError(detail);
        return false;
    };
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
            LuaAssFile snapshot(impl_->file, true);
            const auto result = entry.script->run(int(index), snapshot, selected, active);
            if (!result.error.isEmpty()) return fail(macro.name, result.error);
            if (result.cancelled) return false;
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
            const auto label = (result.undoLabel.isEmpty() ? macro.name : result.undoLabel).toStdString();
            const int flags = snapshot.publish(impl_->file);
            if (flags) commit(&impl_->file, label, flags, -1, nullptr);
            if (flags || result.selection || result.active) impl_->grid.setAutomationSelection(rows, target);
            return true;
        } catch (...) { return fail(macro.name, exceptionMessage()); }
    }
    return false;
}

} // namespace aegisub_qt::automation
