#pragma once

// agi::signal (via ass_file.h) has members named `slots`; keep this before any
// Qt header for the same reason ass_file.h is included first in the .cpp.
#include <libaegisub/signal.h>

#include <QObject>
#include <QString>
#include <QTimer>

class AssDialogue;
class AssFile;
class SubtitleGridModel;
class UndoManager;

/// View model for the single-line edit box at the bottom of the window.
///
/// Reads/writes the AssDialogue selected in SubtitleGridModel. Every write goes
/// through AssFile::Commit so that the UndoManager records it; consecutive
/// edits of the same field on the same line reuse one commit id and therefore
/// collapse into a single undo step.
class EditBoxViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(QString startTime READ startTime WRITE setStartTime NOTIFY startTimeChanged)
    Q_PROPERTY(QString endTime READ endTime WRITE setEndTime NOTIFY endTimeChanged)

public:
    EditBoxViewModel(SubtitleGridModel *model, AssFile *file, UndoManager *undoManager,
                     QObject *parent = nullptr);

    QString text() const;
    void setText(const QString &value);

    QString startTime() const;
    void setStartTime(const QString &value);

    QString endTime() const;
    void setEndTime(const QString &value);

signals:
    void textChanged();
    void startTimeChanged();
    void endTimeChanged();
    /// Emitted when the active line changed behind this view model's back
    /// (undo/redo or a commit from another editor). The QML edit box reloads
    /// its fields; self-commits deliberately do not, so the cursor never jumps
    /// while the user is typing.
    void externalReload();

private:
    AssDialogue *active() const;
    void reload();
    void resetCoalescing();
    void commitEdit(const QString &description, int type, AssDialogue *line);

    /// AssFile::AnnounceCommit slot: an external commit changed the file.
    void onFileCommitted(int type, const AssDialogue *single_line);
    /// UndoManager::restored slot: a snapshot was swapped into the file.
    void onRestored();
    /// Deferred part of onRestored(): runs after the grid rebuilt its rows.
    void refreshAfterExternalChange();

    SubtitleGridModel *model_;
    AssFile *file_;
    UndoManager *undo_manager_;

    /// Single-shot timer that closes the coalescing window for typing.
    QTimer coalesce_timer_;
    /// Commit id to amend on the next commit, or -1 to start a new undo step.
    int commit_id_ = -1;
    int last_type_ = 0;
    int last_row_ = -1;
    /// True while this view model is inside its own AssFile::Commit call.
    bool in_self_commit_ = false;

    agi::signal::Connection commit_connection_;
};
