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
    endResetModel();
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
