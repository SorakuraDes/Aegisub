// Must come before any Qt header: agi::signal (via ass_file.h) has members
// named `slots`, and Qt defines `signals`/`slots` as macros.
#include "../../ass_file.h"
// Also agi-only (libaegisub/option_value.h has no include guard, so it must not
// end up included twice through different orders).
#include "../../options.h"

#include "subtitle_grid_model.h"
#include "undo_manager.h"

#include "../commit_bridge.h"

#include "../../ass_dialogue.h"

#include <libaegisub/ass/time.h>

#include <QString>

#include <algorithm>
#include <string>

namespace {

constexpr int kFallbackDefaultDurationMs = 3000;

/// The wx frontend's "Timing/Default Duration" option (new lines are given that
/// much time), with a fallback for the case where the option store is not up.
int defaultDurationMs() {
    if (!config::opt)
        return kFallbackDefaultDurationMs;
    const int duration = OPT_GET("Timing/Default Duration")->GetInt();
    return duration > 0 ? duration : kFallbackDefaultDurationMs;
}

std::string toUtf8StdString(const QString &s) {
    const QByteArray utf8 = s.toUtf8();
    return std::string(utf8.constData(), size_t(utf8.size()));
}

} // namespace

SubtitleGridModel::SubtitleGridModel(AssFile *file, UndoManager *undoManager, QObject *parent)
: QAbstractListModel(parent)
, file_(file)
, undo_manager_(undoManager)
{
    if (file_)
        commit_connection_ = file_->AddCommitListener(&SubtitleGridModel::onFileCommitted, this);

    // UndoManager is constructed before this model in main.cpp, so connecting
    // here guarantees the model rebuilds rows_ before any later listener of
    // UndoManager::restored asks it for the active line.
    if (undo_manager_)
        connect(undo_manager_, &UndoManager::restored, this, &SubtitleGridModel::onRestored);

    refresh();
}

void SubtitleGridModel::rebuildRows() {
    rows_.clear();
    if (!file_)
        return;
    rows_.reserve(file_->Events.size());
    for (auto &line : file_->Events)
        rows_.push_back(&line);
}

void SubtitleGridModel::refresh() {
    beginResetModel();
    rebuildRows();
    endResetModel();

    // Non-empty models start with the first line selected, empty ones have no
    // selection. Emit only on an actual change so listeners see a stable state.
    const int new_active = rows_.empty() ? -1 : 0;
    if (active_row_ != new_active) {
        active_row_ = new_active;
        emit activeRowChanged();
    }
}

void SubtitleGridModel::refreshPreservingActiveRow() {
    const int previous_active = active_row_;

    beginResetModel();
    rebuildRows();
    endResetModel();

    int new_active;
    if (rows_.empty())
        new_active = -1;
    else
        new_active = std::clamp(previous_active, 0, int(rows_.size()) - 1);

    if (active_row_ != new_active) {
        active_row_ = new_active;
        emit activeRowChanged();
    }
}

int SubtitleGridModel::indexOf(const AssDialogue *line) const {
    if (!line)
        return -1;
    const auto it = std::find(rows_.begin(), rows_.end(), line);
    return it == rows_.end() ? -1 : int(it - rows_.begin());
}

void SubtitleGridModel::onFileCommitted(int type, const AssDialogue *single_line) {
    if (!file_)
        return;

    // Changes to the shape of the line list invalidate the whole index.
    if (type == AssFile::COMMIT_NEW
        || (type & (AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_ORDER | AssFile::COMMIT_STYLES))
               != 0) {
        refreshPreservingActiveRow();
        return;
    }

    // A single line's content changed: touch just that row. The fast path is
    // the currently selected line (the common case for the edit box).
    if (single_line) {
        int row = -1;
        if (active_row_ >= 0 && active_row_ < int(rows_.size())
            && rows_[size_t(active_row_)] == single_line)
            row = active_row_;
        else
            row = indexOf(single_line);

        if (row >= 0) {
            emit dataChanged(index(row), index(row));
            return;
        }
    }

    // [Script Info] / attachment / extradata commits do not change a row's
    // displayed data, so there is nothing to refresh.
    if ((type & AssFile::COMMIT_DIAG_FULL) == 0)
        return;

    // A dialogue change that did not identify the affected line: rebuild.
    refreshPreservingActiveRow();
}

void SubtitleGridModel::onRestored() {
    // swap() replaced every AssDialogue in the file; rows_ is dangling.
    refreshPreservingActiveRow();
}

int SubtitleGridModel::activeRow() const {
    return active_row_;
}

void SubtitleGridModel::setActiveRow(int row) {
    if (row < -1 || row >= int(rows_.size()))
        row = -1;
    if (active_row_ == row)
        return;
    active_row_ = row;
    emit activeRowChanged();
}

AssDialogue *SubtitleGridModel::getActiveDialogue() {
    if (active_row_ < 0 || active_row_ >= int(rows_.size()))
        return nullptr;
    return rows_[size_t(active_row_)];
}

void SubtitleGridModel::notifyRowUpdated(int row) {
    if (row < 0 || row >= int(rows_.size()))
        return;
    emit dataChanged(index(row), index(row));
}

int SubtitleGridModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid())
        return 0;
    return int(rows_.size());
}

QVariant SubtitleGridModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= int(rows_.size()))
        return {};

    const AssDialogue *line = rows_[size_t(index.row())];
    switch (role) {
        case RowNumberRole:
            return index.row() + 1;
        case LayerRole:
            return line->Layer;
        case StartTimeRole:
            return QString::fromStdString(line->Start.GetAssFormatted());
        case EndTimeRole:
            return QString::fromStdString(line->End.GetAssFormatted());
        case StyleRole:
            return QString::fromStdString(line->Style.get());
        case ActorRole:
            return QString::fromStdString(line->Actor.get());
        case EffectRole:
            return QString::fromStdString(line->Effect.get());
        case TextRole:
            return QString::fromStdString(line->Text.get());
        case CommentRole:
            return line->Comment;
        default:
            return {};
    }
}

QHash<int, QByteArray> SubtitleGridModel::roleNames() const {
    return {
        { RowNumberRole, "rowNumber" },
        { LayerRole, "layer" },
        { StartTimeRole, "startTime" },
        { EndTimeRole, "endTime" },
        { StyleRole, "styleName" },
        { ActorRole, "actor" },
        { EffectRole, "effect" },
        { TextRole, "text" },
        { CommentRole, "isComment" },
    };
}

// ---------------------------------------------------------------------------
// Row operations
// ---------------------------------------------------------------------------

void SubtitleGridModel::commitRowChange(const QString &description, AssDialogue *line) {
    if (!file_)
        return;
    // A fresh amend id (-1) for every operation: each insert/delete is its own
    // undo step, unlike the coalesced text edits of EditBoxViewModel.
    aegisub_qt::commit(file_, toUtf8StdString(description), AssFile::COMMIT_DIAG_ADDREM, -1, line);
}

int SubtitleGridModel::selectLine(AssDialogue *line) {
    const int index = indexOf(line);
    if (index >= 0)
        setActiveRow(index);
    return index;
}

int SubtitleGridModel::appendDefaultLine() {
    if (!file_)
        return -1;
    auto *line = new AssDialogue;
    line->End = defaultDurationMs();
    file_->Events.push_back(*line);
    commitRowChange(QStringLiteral("Insert line"), line);
    return selectLine(line);
}

int SubtitleGridModel::insertBefore(int row) {
    if (!file_)
        return -1;
    if (rows_.empty())
        return appendDefaultLine();

    row = std::clamp(row, 0, int(rows_.size()) - 1);
    AssDialogue *current = rows_[size_t(row)];

    auto *line = new AssDialogue;
    line->Style = current->Style;
    // Same rule as subtitle/insert/before: end where the current line starts
    // and give it up to the default duration, but never overlap the previous
    // line and never go negative.
    line->End = current->Start;
    agi::Time start = line->End - defaultDurationMs();
    for (auto const &other : file_->Events) {
        if (other.End <= line->End)
            start = std::max<agi::Time>(start, other.End);
    }
    line->Start = std::max<agi::Time>(start, agi::Time(0));

    auto pos = file_->Events.iterator_to(*current);
    file_->Events.insert(pos, *line);

    commitRowChange(QStringLiteral("Insert line before"), line);
    return selectLine(line);
}

int SubtitleGridModel::insertAfter(int row) {
    if (!file_)
        return -1;
    if (rows_.empty())
        return appendDefaultLine();

    row = std::clamp(row, 0, int(rows_.size()) - 1);
    AssDialogue *current = rows_[size_t(row)];

    auto *line = new AssDialogue;
    line->Style = current->Style;
    // Same rule as subtitle/insert/after: start where the current line ends,
    // limit the duration so it does not run into the next line.
    line->Start = current->End;
    agi::Time end = line->Start + defaultDurationMs();
    for (auto const &other : file_->Events) {
        if (other.Start >= line->Start)
            end = std::min<agi::Time>(end, other.Start);
    }
    line->End = std::max<agi::Time>(end, line->Start);

    auto pos = file_->Events.iterator_to(*current);
    file_->Events.insert(++pos, *line);

    commitRowChange(QStringLiteral("Insert line after"), line);
    return selectLine(line);
}

int SubtitleGridModel::duplicateRow(int row) {
    if (!file_ || row < 0 || row >= int(rows_.size()))
        return -1;

    AssDialogue *source = rows_[size_t(row)];
    // Same as edit/line/duplicate: a plain deep copy inserted right after.
    auto *copy = new AssDialogue(*source);

    auto pos = file_->Events.iterator_to(*source);
    file_->Events.insert(++pos, *copy);

    commitRowChange(QStringLiteral("Duplicate line"), copy);
    return selectLine(copy);
}

void SubtitleGridModel::deleteRow(int row) {
    if (!file_ || rows_.empty())
        return;

    row = std::clamp(row, 0, int(rows_.size()) - 1);
    AssDialogue *target = rows_[size_t(row)];

    // Unlink first, then free: the intrusive list does not own the line.
    // Anything that still points at it is fixed up by the model reset that the
    // Commit below triggers.
    file_->Events.erase(file_->Events.iterator_to(*target));
    delete target;

    // edit/line/delete in the wx frontend always leaves at least one line.
    if (file_->Events.empty()) {
        auto *replacement = new AssDialogue;
        replacement->End = defaultDurationMs();
        file_->Events.push_back(*replacement);
    }

    commitRowChange(QStringLiteral("Delete line"));

    // Commit rebuilt rows_; land on the line which took the deleted one's place,
    // or on the previous line when the last row went away.
    setActiveRow(rows_.empty() ? -1 : std::min(row, int(rows_.size()) - 1));
}
