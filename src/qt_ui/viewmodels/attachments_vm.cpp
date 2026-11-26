#include "../../ass_attachment.h"
#include "../../ass_file.h"
#include "../commit_bridge.h"
#include "attachments_vm.h"
#include "document_vm.h"
#include "undo_manager.h"

#include <libaegisub/ass/uuencode.h>
#include <libaegisub/exception.h>

#include <boost/filesystem/path.hpp>

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <exception>

namespace {

QString localPath(const QUrl &url) {
    if (!url.isValid() || !url.isLocalFile() || url.hasQuery() || url.hasFragment())
        return {};
    const QString path = QDir::fromNativeSeparators(url.toLocalFile());
    if (!QDir::isAbsolutePath(path) || !QFileInfo(path).isAbsolute() || path.contains(QChar::Null)
        || path.startsWith(QLatin1String("//?/")) || path.startsWith(QLatin1String("//./")))
        return {};
    return path;
}

bool safeFilename(const QString &name) {
    // Names stored in scripts must be portable leaf names, not paths, ADS,
    // or DOS device aliases (which remain reserved even with an extension).
    static const QRegularExpression invalid(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f\x7f])"));
    static const QRegularExpression device(
        QStringLiteral(R"(^(?:CON|PRN|AUX|NUL|CONIN\$|CONOUT\$|COM[1-9\x{b9}\x{b2}\x{b3}]|LPT[1-9\x{b9}\x{b2}\x{b3}])$)"),
        QRegularExpression::CaseInsensitiveOption);
    return !name.isEmpty() && !name.contains(QLatin1String(".."))
        && !name.endsWith(QLatin1Char('.')) && !name.endsWith(QLatin1Char(' '))
        && !invalid.match(name).hasMatch()
        && !device.match(name.section(QLatin1Char('.'), 0, 0).trimmed()).hasMatch();
}

agi::fs::path corePath(const QString &path) {
#ifdef Q_OS_WIN
    return agi::fs::path(QDir::toNativeSeparators(path).toStdWString());
#else
    return agi::fs::path(path.toUtf8().toStdString());
#endif
}

} // namespace

AttachmentsViewModel::AttachmentsViewModel(AssFile *file, UndoManager *undoManager,
                                           DocumentViewModel *documentVM, QObject *parent)
: QAbstractListModel(parent), file_(file) {
    if (file_)
        commit_connection_ = file_->AddCommitListener(&AttachmentsViewModel::onFileCommitted, this);
    if (undoManager)
        connect(undoManager, &UndoManager::restored, this, &AttachmentsViewModel::refresh);
    if (documentVM) {
        document_path_ = documentVM->filePath();
        connect(documentVM, &DocumentViewModel::filePathChanged, this, [this, documentVM] {
            document_path_ = documentVM->filePath();
        });
        connect(documentVM, &DocumentViewModel::documentReloaded, this, &AttachmentsViewModel::refresh);
    }
    refresh();
}

int AttachmentsViewModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant AttachmentsViewModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.model() != this || index.column() != 0 || index.row() >= count())
        return {};
    const auto &row = rows_[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case NameRole: return row.name;
    case GroupRole: return row.group;
    case SizeRole: return row.size;
    default: return {};
    }
}

QHash<int, QByteArray> AttachmentsViewModel::roleNames() const {
    return {{NameRole, "name"}, {GroupRole, "group"}, {SizeRole, "size"}};
}

void AttachmentsViewModel::refresh() {
    std::vector<Row> updated;
    if (file_) {
        updated.reserve(file_->Attachments.size());
        for (const auto &attachment : file_->Attachments) {
            updated.push_back({QString::fromStdString(attachment.GetFileName(true)),
                               QString::fromStdString(attachment.GroupHeader()),
                               static_cast<qulonglong>(attachment.GetSize())});
        }
    }
    const bool count_changed = updated.size() != rows_.size();
    clearError();
    beginResetModel();
    rows_ = std::move(updated);
    endResetModel();
    if (count_changed)
        emit countChanged();
}

void AttachmentsViewModel::onFileCommitted(int type, const AssDialogue *) {
    if (type == AssFile::COMMIT_NEW || (type & AssFile::COMMIT_ATTACHMENT))
        refresh();
}

bool AttachmentsViewModel::fail(const QString &message) {
    if (last_error_ != message) {
        last_error_ = message;
        emit lastErrorChanged();
    }
    return false;
}

void AttachmentsViewModel::clearError() {
    if (!last_error_.isEmpty()) {
        last_error_.clear();
        emit lastErrorChanged();
    }
}

bool AttachmentsViewModel::attachFonts(const QList<QUrl> &files) {
    return attachFiles(files, true);
}

bool AttachmentsViewModel::attachGraphics(const QList<QUrl> &files) {
    return attachFiles(files, false);
}

bool AttachmentsViewModel::attachFiles(const QList<QUrl> &files, bool fonts) {
    clearError();
    if (files.isEmpty())
        return false;
    if (!file_)
        return fail(tr("No subtitle document is open."));

    const QStringList extensions = fonts ? QStringList{QStringLiteral("ttf"), QStringLiteral("ttc"), QStringLiteral("pfb")}
        : QStringList{QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("jpg"),
                      QStringLiteral("ico"), QStringLiteral("wmf")};
    QStringList paths;
    for (const auto &url : files) {
        const QString path = localPath(url);
        const QFileInfo info(path);
        if (path.isEmpty() || !info.isFile())
            return fail(tr("Attachment source is not a local file: %1").arg(url.toDisplayString()));
        if (!safeFilename(info.fileName()))
            return fail(tr("Unsafe attachment filename: %1").arg(info.fileName()));
        if (!extensions.contains(info.suffix().toLower()))
            return fail(tr("Unsupported attachment type: %1").arg(info.fileName()));
        paths.push_back(path);
    }

    // InsertAttachment appends one entry at a time. Roll back the whole batch
    // on a read error so the live file and the undo snapshot stay in sync.
    const auto original_size = file_->Attachments.size();
    QString path;
    auto readError = [this, original_size, &path](const QString &reason) {
        while (file_->Attachments.size() > original_size)
            file_->Attachments.pop_back();
        return fail(tr("Could not attach %1: %2").arg(path, reason));
    };
    try {
        for (const auto &source : paths) {
            path = source;
            file_->InsertAttachment(corePath(path));
        }
    }
    catch (const agi::Exception &e) { return readError(QString::fromStdString(e.GetMessage())); }
    catch (const std::exception &e) { return readError(QString::fromUtf8(e.what())); }
    catch (...) { return readError(tr("Unknown read error.")); }

    aegisub_qt::commit(file_, "attach", AssFile::COMMIT_ATTACHMENT);
    return true;
}

bool AttachmentsViewModel::getSelectedRows(const QVariantList &input, std::vector<int> &rows) {
    if (!file_ || input.isEmpty())
        return fail(tr("No attachments selected."));
    rows.reserve(input.size());
    for (const auto &value : input) {
        bool ok = false;
        const int row = value.toInt(&ok);
        if (!ok || value.toDouble() != row || row < 0 || row >= count())
            return fail(tr("The selected attachment no longer exists."));
        rows.push_back(row);
    }
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    return true;
}

bool AttachmentsViewModel::deleteAttachments(const QVariantList &input) {
    clearError();
    std::vector<int> rows;
    if (!getSelectedRows(input, rows))
        return false;
    auto &attachments = file_->Attachments;
    for (auto row = rows.rbegin(); row != rows.rend(); ++row)
        attachments.erase(attachments.begin() + *row);
    aegisub_qt::commit(file_, "delete attachment", AssFile::COMMIT_ATTACHMENT);
    return true;
}

bool AttachmentsViewModel::extractionName(int row, QString &name) {
    if (!file_ || row < 0 || row >= count())
        return fail(tr("The selected attachment no longer exists."));
    const auto &attachment = file_->Attachments[row];
    const auto raw = QString::fromStdString(attachment.GetFileName(true));
    name = QString::fromStdString(attachment.GetFileName());
    if (!safeFilename(raw) || !safeFilename(name))
        return fail(tr("Unsafe attachment filename: %1").arg(raw));
    return true;
}

QUrl AttachmentsViewModel::extractionFileUrl(int row) {
    clearError();
    QString name;
    if (!extractionName(row, name))
        return {};
    const auto directory = document_path_.isEmpty() ? QDir::homePath() : QFileInfo(document_path_).absolutePath();
    return QUrl::fromLocalFile(QDir(directory).absoluteFilePath(name));
}

bool AttachmentsViewModel::validateExtraction(const QVariantList &input) {
    clearError();
    std::vector<int> rows;
    if (!getSelectedRows(input, rows))
        return false;
    QSet<QString> names;
    for (const int row : rows) {
        QString name;
        if (!extractionName(row, name))
            return false;
        // Case/normalization aliases can refer to the same path even if the
        // files are coming from distinct attachment entries.
        const auto key = name.normalized(QString::NormalizationForm_C).toCaseFolded();
        if (names.contains(key))
            return fail(tr("More than one selected attachment extracts as %1.").arg(name));
        names.insert(key);
    }
    return true;
}

bool AttachmentsViewModel::extractAttachment(int row, const QUrl &file) {
    clearError();
    QString name;
    if (!extractionName(row, name))
        return false;
    const QString path = localPath(file);
    if (path.isEmpty() || !safeFilename(QFileInfo(path).fileName()))
        return fail(tr("Invalid extraction file: %1").arg(file.toDisplayString()));
    return writeAttachment(row, path);
}

bool AttachmentsViewModel::extractAttachments(const QVariantList &input, const QUrl &folder) {
    if (!validateExtraction(input))
        return false;
    const QString path = localPath(folder);
    if (path.isEmpty() || !QFileInfo(path).isDir())
        return fail(tr("Extraction folder is not a local directory: %1").arg(folder.toDisplayString()));

    std::vector<int> rows;
    if (!getSelectedRows(input, rows))
        return false;
    QStringList paths;
    for (const int row : rows) {
        const auto name = QString::fromStdString(file_->Attachments[row].GetFileName());
        const auto target = QDir(path).absoluteFilePath(name);
        const QFileInfo existing(target);
        if (existing.exists() || existing.isSymLink())
            return fail(tr("Extraction destination already exists: %1").arg(target));
        paths.push_back(target);
    }
    for (size_t i = 0; i < rows.size(); ++i) {
        if (!writeAttachment(rows[i], paths[static_cast<qsizetype>(i)]))
            return false;
    }
    return true;
}

bool AttachmentsViewModel::writeAttachment(int row, const QString &path) {
    auto writeError = [this, &path](const QString &reason) {
        return fail(tr("Could not extract to %1: %2").arg(path, reason));
    };
    try {
        const QFileInfo destination(path);
        if (destination.isSymLink() || (destination.exists() && !destination.isFile()))
            return writeError(tr("The destination is not a regular file."));

        // Use the core decoder, but not AssAttachment::Extract: its stream
        // does not report failures reliably and it indexes empty decoded data.
        const auto &entry = file_->Attachments[row].GetEntryData();
        const auto header = entry.find('\n');
        if (header == std::string::npos)
            return writeError(tr("The attachment has no data header."));
        const auto decoded = agi::ass::UUDecode(entry.data() + header + 1, entry.data() + entry.size());
        QSaveFile output(path);
        output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly))
            return writeError(output.errorString());
        if (!decoded.empty() && output.write(decoded.data(), static_cast<qint64>(decoded.size()))
                                != static_cast<qint64>(decoded.size()))
            return writeError(output.errorString());
        if (!output.commit())
            return writeError(output.errorString());
        return true;
    }
    catch (const agi::Exception &e) { return writeError(QString::fromStdString(e.GetMessage())); }
    catch (const std::exception &e) { return writeError(QString::fromUtf8(e.what())); }
    catch (...) { return writeError(tr("Unknown write error.")); }
}
