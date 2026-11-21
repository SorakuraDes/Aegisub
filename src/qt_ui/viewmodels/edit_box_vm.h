#pragma once

// agi::signal (via ass_file.h) has members named `slots`; keep this before any
// Qt header for the same reason ass_file.h is included first in the .cpp.
#include <libaegisub/signal.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

class AssDialogue;
class AssFile;
class SubtitleGridModel;
class UndoManager;

/// View model for the edit box at the bottom of the window.
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
    Q_PROPERTY(QString duration READ duration WRITE setDuration NOTIFY durationChanged)
    Q_PROPERTY(bool comment READ comment WRITE setComment NOTIFY commentChanged)
    Q_PROPERTY(int layer READ layer WRITE setLayer NOTIFY layerChanged)
    Q_PROPERTY(QString style READ style WRITE setStyle NOTIFY styleChanged)
    Q_PROPERTY(QString actor READ actor WRITE setActor NOTIFY actorChanged)
    Q_PROPERTY(QString effect READ effect WRITE setEffect NOTIFY effectChanged)
    /// Style names of the current file (never empty; falls back to "Default").
    Q_PROPERTY(QStringList availableStyles READ availableStyles NOTIFY availableStylesChanged)

public:
    EditBoxViewModel(SubtitleGridModel *model, AssFile *file, UndoManager *undoManager,
                     QObject *parent = nullptr);

    QString text() const;
    void setText(const QString &value);

    QString startTime() const;
    void setStartTime(const QString &value);

    QString endTime() const;
    void setEndTime(const QString &value);

    /// End - Start, formatted like the time fields (clamped at zero).
    QString duration() const;
    void setDuration(const QString &value);

    bool comment() const;
    void setComment(bool value);

    int layer() const;
    void setLayer(int value);

    QString style() const;
    void setStyle(const QString &value);

    QString actor() const;
    void setActor(const QString &value);

    QString effect() const;
    void setEffect(const QString &value);

    QStringList availableStyles() const;

    /// Move to the next line, creating one after the current line when the
    /// active line is the last one (keeps the typing flow going).
    Q_INVOKABLE void commitAndNextLine();

    /// Enter / Shift+Enter from the body editor.
    ///
    /// The body is a TextArea, which consumes Return itself, so QML `Keys`
    /// handlers never see it; main.cpp's key filter forwards the key here.
    /// Shift+Enter asks QML to insert a literal "\N"; plain Enter commits and
    /// moves on.
    void handleEnterKey(bool shift);

signals:
    void textChanged();
    void startTimeChanged();
    void endTimeChanged();
    void durationChanged();
    void commentChanged();
    void layerChanged();
    void styleChanged();
    void actorChanged();
    void effectChanged();
    void availableStylesChanged();

    /// Emitted when the active line changed behind this view model's back
    /// (undo/redo or a commit from another editor). The QML edit box reloads
    /// its fields; self-commits deliberately do not, so the cursor never jumps
    /// while the user is typing.
    void externalReload();
    /// Shift+Enter: insert a literal "\N" at the cursor (QML owns the TextArea).
    void lineBreakRequested();
    /// Plain Enter: take focus back and select the (new) active line's text.
    void bodyEditorFocusRequested();

private:
    AssDialogue *active() const;
    void reload();
    void resetCoalescing();
    /// Commit an edit of `line`. `field_key` keeps the coalescing groups of
    /// different metadata fields apart (they all share COMMIT_DIAG_META).
    void commitEdit(const QString &description, int type, AssDialogue *line, int field_key);
    void refreshAvailableStyles();

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
    int last_field_ = 0;
    /// True while this view model is inside its own AssFile::Commit call.
    bool in_self_commit_ = false;

    QStringList available_styles_;

    agi::signal::Connection commit_connection_;
};
