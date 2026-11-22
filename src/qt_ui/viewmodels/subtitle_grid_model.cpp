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

void SubtitleGridModel::emitSelectionChanged() {
    if (rows_.empty())
        return;
    emit dataChanged(index(0), index(int(rows_.size()) - 1), {SelectedRole});
}

void SubtitleGridModel::sanitizeSelection() {
    const int count = int(rows_.size());

    QSet<int> selection;
    for (int row : selected_rows_) {
        if (row >= 0 && row < count)
            selection.insert(row);
    }
    // The grid always has an active line selected while it has lines.
    if (selection.isEmpty() && count > 0)
        selection.insert(active_row_ >= 0 && active_row_ < count ? active_row_ : 0);

    if (selection != selected_rows_) {
        selected_rows_ = selection;
        emit selectionCountChanged();
        emitSelectionChanged();
    }

    if (anchor_row_ < 0 || anchor_row_ >= count)
        anchor_row_ = active_row_;
}

void SubtitleGridModel::refresh() {
    beginResetModel();
    rebuildRows();
    endResetModel();
    emit lineCountChanged();

    // Non-empty models start with the first line selected, empty ones have no
    // selection. Emit only on an actual change so listeners see a stable state.
    const int new_active = rows_.empty() ? -1 : 0;
    const bool active_changed = active_row_ != new_active;

    active_row_ = new_active;
    selected_rows_.clear();
    if (new_active >= 0)
        selected_rows_.insert(new_active);
    anchor_row_ = new_active;

    emit selectionCountChanged();
    emitSelectionChanged();
    if (active_changed)
        emit activeRowChanged();
}

void SubtitleGridModel::refreshPreservingActiveRow() {
    const int previous_active = active_row_;

    beginResetModel();
    rebuildRows();
    endResetModel();
    emit lineCountChanged();

    int new_active;
    if (rows_.empty())
        new_active = -1;
    else
        new_active = std::clamp(previous_active, 0, int(rows_.size()) - 1);

    if (active_row_ != new_active) {
        active_row_ = new_active;
        emit activeRowChanged();
    }

    // The rows moved (or disappeared) under the selection: clamp it.
    sanitizeSelection();
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

    // A content change that did not identify the affected line (batch metadata
    // edit of a multi-row selection, replace-all): repaint the rows in place.
    // A full model reset here would also reset the edit box's coalescing state
    // and turn one typed batch edit into one undo step per keystroke.
    if (!rows_.empty())
        emit dataChanged(index(0), index(int(rows_.size()) - 1));
}

void SubtitleGridModel::onRestored() {
    // swap() replaced every AssDialogue in the file; rows_ is dangling.
    refreshPreservingActiveRow();
}

int SubtitleGridModel::activeRow() const {
    return active_row_;
}

int SubtitleGridModel::selectionCount() const {
    return int(selected_rows_.size());
}

int SubtitleGridModel::lineCount() const {
    return int(rows_.size());
}

const QSet<int> &SubtitleGridModel::selectedRows() const {
    return selected_rows_;
}

QList<int> SubtitleGridModel::sortedSelectedRows() const {
    QList<int> rows = selected_rows_.values();
    std::sort(rows.begin(), rows.end());
    return rows;
}

AssDialogue *SubtitleGridModel::getActiveDialogue() {
    if (active_row_ < 0 || active_row_ >= int(rows_.size()))
        return nullptr;
    return rows_[size_t(active_row_)];
}

AssDialogue *SubtitleGridModel::dialogueAt(int row) const {
    if (row < 0 || row >= int(rows_.size()))
        return nullptr;
    return rows_[size_t(row)];
}

void SubtitleGridModel::applySelection(const QSet<int> &selection, int active) {
    const bool selection_changed = selected_rows_ != selection;
    selected_rows_ = selection;

    const bool active_changed = active_row_ != active;
    active_row_ = active;

    if (selection_changed) {
        emit selectionCountChanged();
        emitSelectionChanged();
    }
    if (active_changed)
        emit activeRowChanged();
}

void SubtitleGridModel::selectOnly(int row) {
    QSet<int> selection;
    if (row >= 0)
        selection.insert(row);
    anchor_row_ = row;
    applySelection(selection, row);
}

void SubtitleGridModel::selectSingle(int row) {
    if (row < -1 || row >= int(rows_.size()))
        row = -1;
    selectOnly(row);
}

void SubtitleGridModel::toggleSelect(int row) {
    if (row < 0 || row >= int(rows_.size()))
        return;

    QSet<int> selection = selected_rows_;
    if (selection.contains(row))
        selection.remove(row);
    else
        selection.insert(row);

    // Keep at least one line selected while the grid has lines.
    if (selection.isEmpty())
        selection.insert(row);

    int active = row;
    if (!selection.contains(active))
        active = *std::min_element(selection.begin(), selection.end());

    anchor_row_ = row;
    applySelection(selection, active);
}

void SubtitleGridModel::selectRange(int row) {
    if (rows_.empty())
        return;

    row = std::clamp(row, 0, int(rows_.size()) - 1);
    const int anchor = (anchor_row_ >= 0 && anchor_row_ < int(rows_.size())) ? anchor_row_ : row;
    const int lo = std::min(anchor, row);
    const int hi = std::max(anchor, row);

    QSet<int> selection;
    for (int i = lo; i <= hi; ++i)
        selection.insert(i);

    // The anchor stays where the range started, so a further Shift+Click grows
    // or shrinks the same range.
    anchor_row_ = anchor;
    applySelection(selection, row);
}

void SubtitleGridModel::selectAll() {
    const int count = int(rows_.size());
    if (count == 0)
        return;

    QSet<int> selection;
    for (int i = 0; i < count; ++i)
        selection.insert(i);

    const int active = (active_row_ >= 0 && active_row_ < count) ? active_row_ : 0;
    if (anchor_row_ < 0 || anchor_row_ >= count)
        anchor_row_ = active;
    applySelection(selection, active);
}

void SubtitleGridModel::setActiveRow(int row) {
    if (row < -1 || row >= int(rows_.size()))
        row = -1;

    // Moving the active row with the keyboard collapses the selection to it.
    anchor_row_ = row;
    QSet<int> selection;
    if (row >= 0)
        selection.insert(row);
    applySelection(selection, row);
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
        case SelectedRole:
            return selected_rows_.contains(index.row());
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
        { SelectedRole, "isSelected" },
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
        selectOnly(index);
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

int SubtitleGridModel::duplicateSelectedRows() {
    if (!file_ || rows_.empty())
        return 0;

    QList<int> selected = sortedSelectedRows();
    if (selected.isEmpty()) {
        if (active_row_ < 0)
            return 0;
        selected << active_row_;
    }

    // Take the pointers first: the row indices shift with every insert, the
    // AssDialogue pointers do not.
    std::vector<AssDialogue *> targets;
    for (int row : selected) {
        if (row >= 0 && row < int(rows_.size()))
            targets.push_back(rows_[size_t(row)]);
    }
    if (targets.empty())
        return 0;

    std::vector<AssDialogue *> copies;
    for (auto *source : targets) {
        auto *copy = new AssDialogue(*source);
        auto pos = file_->Events.iterator_to(*source);
        file_->Events.insert(++pos, *copy);
        copies.push_back(copy);
    }

    // One commit for the whole batch: a single Ctrl+Z undoes all copies.
    commitRowChange(QStringLiteral("Duplicate lines"));

    QSet<int> selection;
    for (auto *copy : copies) {
        const int index = indexOf(copy);
        if (index >= 0)
            selection.insert(index);
    }

    int active = -1;
    if (!selection.isEmpty())
        active = *std::min_element(selection.begin(), selection.end());
    anchor_row_ = active;
    applySelection(selection, active);

    return int(copies.size());
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
    selectOnly(rows_.empty() ? -1 : std::min(row, int(rows_.size()) - 1));
}

int SubtitleGridModel::deleteSelectedRows() {
    if (!file_ || rows_.empty())
        return 0;

    QList<int> selected = sortedSelectedRows();
    if (selected.isEmpty()) {
        if (active_row_ < 0)
            return 0;
        selected << active_row_;
    }

    std::vector<AssDialogue *> targets;
    for (int row : selected) {
        if (row >= 0 && row < int(rows_.size()))
            targets.push_back(rows_[size_t(row)]);
    }
    if (targets.empty())
        return 0;

    // Unlink everything before freeing anything, so the intrusive list is
    // consistent for the UndoManager snapshot taken by Commit.
    for (auto *target : targets)
        file_->Events.erase(file_->Events.iterator_to(*target));
    for (auto *target : targets)
        delete target;

    if (file_->Events.empty()) {
        auto *replacement = new AssDialogue;
        replacement->End = defaultDurationMs();
        file_->Events.push_back(*replacement);
    }

    // One commit for the whole batch: a single Ctrl+Z brings every line back.
    commitRowChange(QStringLiteral("Delete lines"));

    selectOnly(rows_.empty() ? -1 : std::min(selected.first(), int(rows_.size()) - 1));
    return int(targets.size());
}
