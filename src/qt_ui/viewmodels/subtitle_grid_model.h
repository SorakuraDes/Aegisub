#pragma once

#include <QAbstractListModel>
#include <QString>

#include <vector>

class AssDialogue;
class AssFile;

/// Read-only list model over the dialogue lines of the current AssFile.
///
/// The AssFile stores events in a boost intrusive list, which is not
/// random-access, so a std::vector<AssDialogue*> index is built once at
/// construction (and on refresh()) to give O(1) row lookup.
class SubtitleGridModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        RowNumberRole = Qt::UserRole + 1,
        LayerRole,
        StartTimeRole,
        EndTimeRole,
        StyleRole,
        ActorRole,
        EffectRole,
        TextRole,
        CommentRole,
    };
    Q_ENUM(Roles)

    explicit SubtitleGridModel(AssFile *file, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Rebuild the index table from the current AssFile contents.
    Q_INVOKABLE void refresh();

private:
    AssFile *file_;
    std::vector<AssDialogue *> rows_;
};
