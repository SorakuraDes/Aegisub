// Must come before any Qt header: agi::signal (via ass_file.h) has members
// named `slots`, and Qt defines `signals`/`slots` as macros.
#include "../../ass_file.h"

#include "subtitle_grid_model.h"

#include "../../ass_dialogue.h"

#include <libaegisub/ass/time.h>

#include <QString>

SubtitleGridModel::SubtitleGridModel(AssFile *file, QObject *parent)
: QAbstractListModel(parent)
, file_(file)
{
    refresh();
}

void SubtitleGridModel::refresh() {
    beginResetModel();
    rows_.clear();
    if (file_) {
        rows_.reserve(file_->Events.size());
        for (auto &line : file_->Events)
            rows_.push_back(&line);
    }
    if (active_row_ >= int(rows_.size()))
        active_row_ = -1;
    endResetModel();
    emit activeRowChanged();
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
