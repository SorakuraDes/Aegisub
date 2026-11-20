#pragma once

#include <QAbstractListModel>
#include <QString>

#include <vector>

class AssDialogue;
class AssFile;

/// List model over the dialogue lines of the current AssFile.
///
/// The AssFile stores events in a boost intrusive list, which is not
/// random-access, so a std::vector<AssDialogue*> index is built at construction
/// (and on refresh()) to give O(1) row lookup.
class SubtitleGridModel final : public QAbstractListModel {
    Q_OBJECT
    /// Currently selected row (-1 when nothing is selected).
    Q_PROPERTY(int activeRow READ activeRow WRITE setActiveRow NOTIFY activeRowChanged)

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

    int activeRow() const;
    void setActiveRow(int row);

    /// The AssDialogue behind the selected row, or nullptr.
    AssDialogue *getActiveDialogue();

    /// Rebuild the index table from the current AssFile contents.
    Q_INVOKABLE void refresh();

    /// Tell views that the data of one row changed (after editing the
    /// AssDialogue behind it).
    Q_INVOKABLE void notifyRowUpdated(int row);

signals:
    void activeRowChanged();

private:
    AssFile *file_;
    std::vector<AssDialogue *> rows_;
    int active_row_ = -1;
};
