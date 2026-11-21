#pragma once

// agi::signal has members named `slots`, which Qt's `#define slots public`
// would mangle; parse it before any Qt header.
#include <libaegisub/signal.h>

#include <QAbstractListModel>
#include <QString>

#include <vector>

class AssDialogue;
class AssFile;
class UndoManager;

/// List model over the dialogue lines of the current AssFile.
///
/// The AssFile stores events in a boost intrusive list, which is not
/// random-access, so a std::vector<AssDialogue*> index is built at construction
/// (and whenever the file's line list changes) to give O(1) row lookup.
///
/// The model follows AssFile::AnnounceCommit and UndoManager::restored, so view
/// models no longer have to tell it which row changed.
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

    SubtitleGridModel(AssFile *file, UndoManager *undoManager, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int activeRow() const;
    void setActiveRow(int row);

    /// The AssDialogue behind the selected row, or nullptr.
    AssDialogue *getActiveDialogue();

    /// Rebuild the index table from the current AssFile contents and select the
    /// first line (used when a document is created or opened).
    Q_INVOKABLE void refresh();

    /// Tell views that the data of one row changed (after editing the
    /// AssDialogue behind it).
    Q_INVOKABLE void notifyRowUpdated(int row);

signals:
    void activeRowChanged();

private:
    void rebuildRows();
    int indexOf(const AssDialogue *line) const;
    /// Rebuild the row index but keep the selected row (clamped on shrink).
    void refreshPreservingActiveRow();

    /// AssFile::AnnounceCommit slot: a commit changed the file.
    void onFileCommitted(int type, const AssDialogue *single_line);
    /// UndoManager::restored slot: a snapshot was swapped into the file, so
    /// every AssDialogue pointer in rows_ is dangling.
    void onRestored();

    AssFile *file_;
    UndoManager *undo_manager_;
    std::vector<AssDialogue *> rows_;
    int active_row_ = -1;
    agi::signal::Connection commit_connection_;
};
