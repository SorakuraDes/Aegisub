// Must come before any Qt header: agi::signal (via ass_file.h) has members
// named `slots`, and Qt defines `signals`/`slots` as macros.
#include "../../ass_file.h"
// For the blank default line added when a .srt has no cues at all.
#include "../../ass_dialogue.h"
#include "../../options.h"
#include <libaegisub/path.h>

#include "../aegisub_core.h"
#include "../qt_path.h"

#include "document_vm.h"
#include "subtitle_grid_model.h"
#include "undo_manager.h"

#include "../subtitle_format_ass.h"
#include "../subtitle_format_srt.h"

#include <libaegisub/charset.h>
#include <libaegisub/exception.h>
#include <libaegisub/vfr.h>

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QScopedValueRollback>

#include <algorithm>
#include <cmath>

#include <exception>
#include <string>
#include <unordered_set>

namespace {

/// SubRip import/export is selected purely by the file name suffix; matching is
/// case-insensitive so `Movie.SRT` opens in the SRT reader too.
bool isSrtPath(const QString &path) {
    return QFileInfo(path).suffix().compare("srt", Qt::CaseInsensitive) == 0;
}

QString localPath(const std::string &path, const QDir &base) {
    if (path.empty()) return {};
    auto result = QString::fromStdString(path);
    if (result.startsWith(QStringLiteral("?script/")) || result.startsWith(QStringLiteral("?script\\")))
        result = result.mid(8);
    else if (result.startsWith(QLatin1Char('?')) && config::path) {
        result.replace(QLatin1Char('\\'), QLatin1Char('/'));
        const auto relative = result.indexOf(QLatin1Char('/'));
        // Decode only the token; preserve Unicode in the remainder of the path.
        const auto token = result.left(relative < 0 ? result.size() : relative);
        const auto tokenPath = config::path->Decode(token.toStdString());
#ifdef _WIN32
        const auto dir = QString::fromStdWString(tokenPath.wstring());
#else
        const auto dir = QString::fromStdString(tokenPath.string());
#endif
        if (QDir::isAbsolutePath(dir))
            result = relative < 0 ? dir : QDir(dir).absoluteFilePath(result.mid(relative + 1));
    }
    if (result.startsWith(QLatin1Char('?'))) return {};
    return QDir::cleanPath(base.absoluteFilePath(result));
}

// Omit obsolete fold values from the save copy, not from the undo snapshots.
void pruneUnusedFolds(AssFile &file) {
    std::unordered_set<uint32_t> referenced;
    for (const auto &line : file.Events)
        for (auto id : line.ExtradataIds.get())
            referenced.insert(id);
    auto &entries = file.Extradata;
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const auto &entry) {
        return entry.key == "_aegi_folddata" && !referenced.count(entry.id);
    }), entries.end());
}

} // namespace

DocumentViewModel::DocumentViewModel(AssFile *file, SubtitleGridModel *gridModel,
                                     UndoManager *undoManager, QObject *parent)
: QObject(parent)
, file_(file)
, gridModel_(gridModel)
, undoManager_(undoManager)
{
    if (undoManager_) {
        // dirty is defined as "the undo position differs from the saved one",
        // so undo/redo update the title bar without any extra bookkeeping.
        connect(undoManager_, &UndoManager::isModifiedChanged,
                this, &DocumentViewModel::dirtyChanged);
        connect(undoManager_, &UndoManager::contentEdited, this, [this] {
            autosave_pending_ = true;
            if (config::opt && OPT_GET("App/Auto/Save on Every Change")->GetBool() && !file_path_.isEmpty())
                save_change_timer_.start();
        });
        connect(undoManager_, &UndoManager::restored, this, [this] {
            autosave_pending_ = dirty();
            if (file_) updateSaveProperties(*file_, file_path_);
        });
    }
    save_change_timer_.setSingleShot(true);
    save_change_timer_.setInterval(0);
    connect(&save_change_timer_, &QTimer::timeout, this, [this] {
        // Do not write the file from inside AssFile::Commit's callbacks.
        if (!file_ || !dirty() || !config::opt || !OPT_GET("App/Auto/Save on Every Change")->GetBool()) return;
        // Other formats may discard ASS data even when their writer accepts it.
        AssSubtitleFormat format;
        if (QFileInfo(file_path_).suffix().compare("ass", Qt::CaseInsensitive) == 0 && format.CanSave(file_))
            saveFile();
        else
            writeAutosave();
    });
    connect(&autosave_timer_, &QTimer::timeout, this, &DocumentViewModel::writeAutosave);
    if (config::opt) {
        autosave_enabled_subscription_ = OPT_SUB("App/Auto/Save", &DocumentViewModel::updateAutosaveTimer, this);
        autosave_interval_subscription_ = OPT_SUB("App/Auto/Save Every Seconds", &DocumentViewModel::updateAutosaveTimer, this);
        updateAutosaveTimer();
    }
}

void DocumentViewModel::updateAutosaveTimer() {
    if (!config::opt || !OPT_GET("App/Auto/Save")->GetBool()) {
        autosave_timer_.stop();
        return;
    }
    const auto seconds = std::clamp<int64_t>(OPT_GET("App/Auto/Save Every Seconds")->GetInt(), 1, 2147483);
    autosave_timer_.start(int(seconds * 1000));
}

void DocumentViewModel::writeAutosave() {
    if (!file_ || !dirty() || !autosave_pending_) return;
    try {
        const auto base = file_path_.isEmpty() ? QDir(QFileInfo(aegisub_qt::OptionsFilePath()).absolutePath())
                                                : QFileInfo(file_path_).absoluteDir();
        auto dir = localPath(OPT_GET("Path/Auto/Save")->GetString(), base);
        if (dir.isEmpty()) dir = base.absolutePath();
        if (!QDir().mkpath(dir)) {
            qWarning() << "[autosave] Could not create" << dir;
            return;
        }
        const auto stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd-HH-mm-ss-zzz"));
        const auto path = QDir(dir).filePath(displayName() + QLatin1Char('.') + stamp + QStringLiteral(".AUTOSAVE.ass"));
        AssSubtitleFormat format;
        AssFile copy(*file_);
        copy.Properties = file_->Properties;
        updateSaveProperties(copy, path);
        pruneUnusedFolds(copy);
        format.WriteFile(&copy, aegisub_qt::ToNativePath(path), agi::vfr::Framerate(), "UTF-8");
        autosave_pending_ = false;
        qInfo() << "[autosave] saved" << path;
    } catch (const agi::Exception &e) {
        qWarning() << "[autosave]" << QString::fromStdString(e.GetMessage());
    } catch (const std::exception &e) {
        qWarning() << "[autosave]" << e.what();
    }
}

void DocumentViewModel::backupOriginal(const QString &path) {
    if (!config::opt || !OPT_GET("App/Auto/Backup")->GetBool()) return;
    const QFileInfo original(path);
    auto dir = localPath(OPT_GET("Path/Auto/Backup")->GetString(), original.absoluteDir());
    if (dir.isEmpty()) dir = original.absolutePath();
    const auto target = QDir(dir).filePath(original.completeBaseName() + QStringLiteral(".ORIGINAL.") + original.suffix());
    QFile input(path);
    QSaveFile output(target);
    if (!QDir().mkpath(dir) || !input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
        qWarning() << "[backup] Could not create" << target;
        return;
    }
    while (!input.atEnd()) {
        const auto chunk = input.read(65536);
        if (chunk.isEmpty() || output.write(chunk) != chunk.size()) {
            qWarning() << "[backup] Could not copy" << target;
            return;
        }
    }
    if (!output.commit()) qWarning() << "[backup] Could not save" << target;
}

void DocumentViewModel::requestLinkedMedia(const QString &path) {
    if (!config::opt || isSrtPath(path)) return;
    const auto mode = OPT_GET("App/Auto/Load Linked Files")->GetInt();
    if (mode != 1 && mode != 2) return;
    const auto base = QFileInfo(path).absoluteDir();
    const auto video = localPath(file_->Properties.video_file, base);
    const auto audio = localPath(file_->Properties.audio_file, base);
    const bool restoreUi = OPT_GET("App/Save UI State")->GetBool();
    const int savedZoom = restoreUi && std::isfinite(video_zoom_)
        ? int(std::lround(std::clamp(video_zoom_, 0., 2.) * 100)) : 0;
    const int zoomPercent = savedZoom == 50 || savedZoom == 100 || savedZoom == 200 ? savedZoom : 0;
    emit linkedMediaRequested(video, audio, mode == 2, zoomPercent, restoreUi ? video_frame_ : -1);
}

void DocumentViewModel::setLinkedVideoPath(const QString &path) {
    if (loading_document_) return;
    linked_video_path_ = path;
    if (file_) updateSaveProperties(*file_, file_path_);
}

void DocumentViewModel::setLinkedAudioPath(const QString &path) {
    if (loading_document_) return;
    linked_audio_path_ = path;
    if (file_) updateSaveProperties(*file_, file_path_);
}

void DocumentViewModel::setVideoUiState(double zoom, int frame) {
    if (!file_ || loading_document_) return;
    file_->Properties.video_zoom = video_zoom_ = zoom;
    file_->Properties.video_position = video_frame_ = frame;
}

void DocumentViewModel::setGridScrollPosition(int row) {
    if (file_ && !loading_document_)
        file_->Properties.scroll_position = grid_scroll_position_ = std::max(0, row);
}

void DocumentViewModel::updateSaveProperties(AssFile &file, const QString &path) {
    if (gridModel_) file.Properties.active_row = std::max(0, gridModel_->activeRow());
    file.Properties.video_zoom = video_zoom_;
    file.Properties.video_position = video_frame_;
    file.Properties.scroll_position = grid_scroll_position_;
    const auto base = QFileInfo(path).absoluteDir();
    const auto relative = [&](const QString &value) {
        if (value.isEmpty()) return std::string();
        return (path.isEmpty() ? value : base.relativeFilePath(value)).toStdString();
    };
    file.Properties.video_file = relative(linked_video_path_);
    file.Properties.audio_file = relative(linked_audio_path_);
}

QString DocumentViewModel::filePath() const {
    return file_path_;
}

QString DocumentViewModel::displayName() const {
    if (file_path_.isEmpty())
        return QStringLiteral("Untitled");
    return QFileInfo(file_path_).fileName();
}

bool DocumentViewModel::dirty() const {
    return undoManager_ ? undoManager_->isModified() : false;
}

QString DocumentViewModel::lastError() const {
    return last_error_;
}

QString DocumentViewModel::encoding() const {
    return encoding_;
}

void DocumentViewModel::setEncoding(const QString &encoding) {
    if (encoding_ == encoding)
        return;
    encoding_ = encoding;
    emit encodingChanged();
}

void DocumentViewModel::setFilePath(const QString &path) {
    if (file_path_ == path)
        return;
    file_path_ = path;
    emit filePathChanged();
    emit displayNameChanged();
}

void DocumentViewModel::setError(const QString &message) {
    last_error_ = message;
    emit lastErrorChanged();
    qWarning() << "[document]" << message;
}

void DocumentViewModel::clearErrorIfNeeded() {
    if (last_error_.isEmpty())
        return;
    last_error_.clear();
    emit lastErrorChanged();
}

void DocumentViewModel::clearError() {
    clearErrorIfNeeded();
}

bool DocumentViewModel::newDocument() {
    if (!file_)
        return false;
    try {
        AssFile tmp;
        tmp.LoadDefault();
        QScopedValueRollback<bool> rebuilding(loading_document_, true);
        emit documentAboutToReload();
        file_->swap(tmp);
        save_change_timer_.stop();
        autosave_pending_ = false;
        // A new script starts with the currently open workspace media.
        grid_scroll_position_ = 0;
        setFilePath(QString());
        clearErrorIfNeeded();
        if (gridModel_)
            gridModel_->refresh();
        updateSaveProperties(*file_, file_path_);
        // A brand new document is clean: the current contents are the baseline
        // every future undo returns to.
        if (undoManager_)
            undoManager_->resetToClean();
        setEncoding(QStringLiteral("UTF-8"));
        emit documentReloaded();
        return true;
    } catch (agi::Exception const &e) {
        setError(QString::fromStdString(e.GetMessage()));
        return false;
    } catch (std::exception const &e) {
        setError(QString::fromUtf8(e.what()));
        return false;
    }
}

bool DocumentViewModel::loadFromPath(const QString &path) {
    if (!file_)
        return false;
    if (!QFileInfo(path).isFile()) {
        setError(QStringLiteral("File not found: ") + path);
        return false;
    }

    try {
        const agi::fs::path native = aegisub_qt::ToNativePath(path);

        // TextFileReader treats an empty encoding as a literal charset name
        // (not auto-detect), so detect it the same way the wx frontend does
        // and fall back to UTF-8 when detection is inconclusive.
        std::string encoding = agi::charset::Detect(native);
        if (encoding.empty())
            encoding = "UTF-8";

        // Parse into a temporary file first; only swap it in on success so a
        // broken file can never destroy the currently open document.
        AssFile tmp;
        if (isSrtPath(path)) {
            // SubRip has no [Script Info]/[V4+ Styles]: ReadFile installs the
            // default ones (plus the SRT default style catalog) before pushing
            // the parsed cues, so the rest of the pipeline is identical to ASS.
            SRTSubtitleFormat format;
            format.ReadFile(&tmp, native, agi::vfr::Framerate(), encoding);
            // An empty .srt would leave a zero-row document. Add the usual blank
            // default line instead of calling LoadDefault(true) again: that
            // would append a second copy of every [Script Info] entry and of the
            // `Default` style, which ReadFile has already installed above.
            if (tmp.Events.empty())
                tmp.Events.push_back(*new AssDialogue);
        }
        else {
            AssSubtitleFormat format;
            format.ReadFile(&tmp, native, agi::vfr::Framerate(), encoding);
        }

        QScopedValueRollback<bool> rebuilding(loading_document_, true);
        emit documentAboutToReload();
        file_->swap(tmp);
        save_change_timer_.stop();
        autosave_pending_ = false;
        const auto base = QFileInfo(path).absoluteDir();
        linked_video_path_ = localPath(file_->Properties.video_file, base);
        linked_audio_path_ = localPath(file_->Properties.audio_file, base);
        const bool restoreUi = config::opt && OPT_GET("App/Save UI State")->GetBool();
        video_zoom_ = restoreUi ? file_->Properties.video_zoom : 0.;
        video_frame_ = restoreUi ? file_->Properties.video_position : 0;
        grid_scroll_position_ = restoreUi ? std::max(0, file_->Properties.scroll_position) : 0;
        setFilePath(path);
        clearErrorIfNeeded();
        if (gridModel_) {
            gridModel_->refresh();
            const int activeRow = file_->Properties.active_row;
            if (restoreUi) gridModel_->restoreActiveRow(activeRow);
        }
        // The freshly loaded contents are the clean baseline: undo stops here.
        if (undoManager_)
            undoManager_->resetToClean();
        setEncoding(QString::fromStdString(encoding));
        backupOriginal(path);
        emit documentReloaded();
        loading_document_ = false;
        emit fileOpened(QFileInfo(path).absoluteFilePath());
        requestLinkedMedia(path);
        qInfo() << "[document] loaded" << path << "encoding" << QString::fromStdString(encoding)
                << "lines" << int(file_->Events.size());
        return true;
    } catch (agi::Exception const &e) {
        setError(QString::fromStdString(e.GetMessage()));
        return false;
    } catch (std::exception const &e) {
        setError(QString::fromUtf8(e.what()));
        return false;
    }
}

bool DocumentViewModel::openFile(const QUrl &url) {
    if (!url.isLocalFile()) {
        setError(QStringLiteral("Only local files are supported"));
        return false;
    }
    return loadFromPath(url.toLocalFile());
}

bool DocumentViewModel::openLocalPath(const QString &path) {
    return loadFromPath(path);
}

bool DocumentViewModel::saveToPath(const QString &path) {
    if (!file_)
        return false;
    try {
        if (isSrtPath(path)) {
            // The SRT writer converts tags, drops comments/empty lines and
            // splits/merges overlapping and identical lines itself; only plain
            // UTF-8 output is supported (no encoding picker yet).
            SRTSubtitleFormat format;
            format.WriteFile(file_, aegisub_qt::ToNativePath(path), agi::vfr::Framerate(), "UTF-8");
        }
        else {
            AssSubtitleFormat format;
            AssFile copy(*file_);
            copy.Properties = file_->Properties;
            updateSaveProperties(copy, path);
            pruneUnusedFolds(copy);
            format.WriteFile(&copy, aegisub_qt::ToNativePath(path), agi::vfr::Framerate(), "UTF-8");
        }
        setFilePath(path);
        updateSaveProperties(*file_, path);
        autosave_pending_ = false;
        save_change_timer_.stop();
        // The undo position at save time becomes the "clean" reference, so the
        // title bar drops the '*' and undoing back here clears it again.
        if (undoManager_)
            undoManager_->markSaved();
        // Saving always writes UTF-8 (both format writers are called with it).
        setEncoding(QStringLiteral("UTF-8"));
        clearErrorIfNeeded();
        qInfo() << "[document] saved" << path;
        return true;
    } catch (agi::Exception const &e) {
        setError(QString::fromStdString(e.GetMessage()));
        return false;
    } catch (std::exception const &e) {
        setError(QString::fromUtf8(e.what()));
        return false;
    }
}

bool DocumentViewModel::saveFile() {
    if (file_path_.isEmpty())
        return false;
    return saveToPath(file_path_);
}

bool DocumentViewModel::saveFileAs(const QUrl &url) {
    if (!url.isLocalFile()) {
        setError(QStringLiteral("Only local files are supported"));
        return false;
    }
    return saveToPath(url.toLocalFile());
}

bool DocumentViewModel::saveLocalPath(const QString &path) {
    return saveToPath(path);
}
