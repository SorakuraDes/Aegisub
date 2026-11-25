// Must come before any Qt header: agi::signal (via ass_file.h) has members
// named `slots`, and Qt defines `signals`/`slots` as macros.
#include "../../ass_file.h"
// For the blank default line added when a .srt has no cues at all.
#include "../../ass_dialogue.h"

#include "../qt_path.h"

#include "document_vm.h"
#include "subtitle_grid_model.h"
#include "undo_manager.h"

#include "../subtitle_format_ass.h"
#include "../subtitle_format_srt.h"

#include <libaegisub/charset.h>
#include <libaegisub/exception.h>
#include <libaegisub/vfr.h>

#include <QDebug>
#include <QFileInfo>

#include <exception>
#include <string>

namespace {

/// SubRip import/export is selected purely by the file name suffix; matching is
/// case-insensitive so `Movie.SRT` opens in the SRT reader too.
bool isSrtPath(const QString &path) {
    return QFileInfo(path).suffix().compare("srt", Qt::CaseInsensitive) == 0;
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
    }
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
        file_->swap(tmp);
        setFilePath(QString());
        clearErrorIfNeeded();
        if (gridModel_)
            gridModel_->refresh();
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

        file_->swap(tmp);
        setFilePath(path);
        clearErrorIfNeeded();
        if (gridModel_)
            gridModel_->refresh();
        // The freshly loaded contents are the clean baseline: undo stops here.
        if (undoManager_)
            undoManager_->resetToClean();
        setEncoding(QString::fromStdString(encoding));
        emit documentReloaded();
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
            format.WriteFile(file_, aegisub_qt::ToNativePath(path), agi::vfr::Framerate(), "UTF-8");
        }
        setFilePath(path);
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
