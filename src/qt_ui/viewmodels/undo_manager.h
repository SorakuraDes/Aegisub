#pragma once

// agi::signal (pulled in by ass_file.h) has members named `slots`, and Qt
// defines `signals`/`slots` as macros, so libaegisub/signal.h has to be parsed
// before any Qt header. Keeping it first in this header makes the order safe
// even for translation units that do not include ass_file.h themselves.
#include <libaegisub/signal.h>

#include <QObject>
#include <QString>

#include <deque>
#include <memory>
#include <string>

class AssDialogue;
class AssFile;

/// Lightweight undo/redo stack for the Qt frontend.
///
/// Aegisub's own undo stack lives in SubsController (wxWidgets) and needs an
/// agi::Context plus the wx UI, so it cannot be reused from `aegisub-qt`.
/// This class is the Qt equivalent of SubsController::OnCommit: it subscribes
/// to AssFile::PushState and keeps deep copies of the AssFile as snapshots.
///
/// Snapshot layout: entries_[0] is the clean baseline, entries_[i] (i > 0) is
/// the file state right after commit i. pos_ is the snapshot currently applied
/// to the file, so undo() moves pos_ back one entry and re-applies it.
///
/// Coalescing mirrors the wx frontend: a caller which passes back the commit id
/// returned by its previous Commit() call folds the follow-up change into the
/// same undo step instead of pushing a new one.
class UndoManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY canRedoChanged)
    Q_PROPERTY(QString undoDescription READ undoDescription NOTIFY undoDescriptionChanged)
    Q_PROPERTY(QString redoDescription READ redoDescription NOTIFY redoDescriptionChanged)
    /// pos_ != saved_pos_: the single source of truth for DocumentViewModel::dirty.
    Q_PROPERTY(bool isModified READ isModified NOTIFY isModifiedChanged)

public:
    explicit UndoManager(AssFile *file, QObject *parent = nullptr);
    ~UndoManager() override;

    int undoLevels() const { return undo_levels_; }
    bool canUndo() const;
    bool canRedo() const;
    QString undoDescription() const;
    QString redoDescription() const;
    bool isModified() const;

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    /// Forget all history and use the current file contents as the clean
    /// baseline. Called after New/Open (and from the constructor).
    void resetToClean();

    /// Remember the current position as the saved state (after a successful
    /// save). Also stops future commits from coalescing into the saved entry,
    /// which would otherwise silently keep the document "clean".
    void markSaved();

signals:
    void canUndoChanged();
    void canRedoChanged();
    void undoDescriptionChanged();
    void redoDescriptionChanged();
    void isModifiedChanged();
    void contentEdited();

    /// Emitted after undo()/redo() swapped a snapshot into the AssFile.
    ///
    /// Every AssDialogue pointer held elsewhere is dangling by then, so
    /// listeners must rebuild their indexes before touching the file. The grid
    /// model connects first (it is constructed first in main.cpp) so that later
    /// listeners can safely ask it for the new active line.
    void restored();

private:
    struct Entry {
        std::shared_ptr<AssFile> file;
        QString description;
        int commit_id = 0;
    };

    int undo_levels_ = 100;
    agi::signal::Connection undo_levels_subscription_;

    void onPushState(const std::string &description, int *commit_id);
    std::shared_ptr<AssFile> makeSnapshot() const;
    void restoreTo(int pos);
    void trimHistory();
    void notifyAll();

    AssFile *file_;
    std::deque<Entry> entries_;
    /// Index into entries_ of the snapshot currently applied to *file_.
    int pos_ = 0;
    /// Index of the snapshot that matches the last successful save, or -1 when
    /// it has been trimmed away (the document then stays modified).
    int saved_pos_ = 0;
    int next_commit_id_ = 1;
    /// Cleared when coalescing would be wrong (after undo/redo/save/reset).
    bool coalesce_allowed_ = false;
    agi::signal::Connection push_state_connection_;
};
