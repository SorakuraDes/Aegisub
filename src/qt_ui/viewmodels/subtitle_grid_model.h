#pragma once

// agi::signal has members named `slots`, which Qt's `#define slots public`
// would mangle; parse it before any Qt header.
#include <libaegisub/signal.h>
#include "../fold_state.h"

#include <QAbstractListModel>
#include <QList>
#include <QPointer>
#include <QSet>
#include <QString>

#include <vector>

class AssDialogue;
class AssFile;
class UndoManager;
class VideoViewModel;

/// List model over the dialogue lines of the current AssFile.
///
/// The AssFile stores events in a boost intrusive list, which is not
/// random-access, so a std::vector<AssDialogue*> index is built at construction
/// (and whenever the file's line list changes) to give O(1) row lookup.
///
/// The model follows AssFile::AnnounceCommit and UndoManager::restored, so view
/// models no longer have to tell it which row changed. It also owns the grid's
/// selection: `activeRow` (the line the edit box works on) plus an arbitrary
/// set of additionally selected rows for batch operations.
class SubtitleGridModel final : public QAbstractListModel {
    Q_OBJECT
    /// Focused document row (-1 when nothing is selected).
    Q_PROPERTY(int activeRow READ activeRow WRITE setActiveRow NOTIFY activeRowChanged)
    /// Number of rows in the selection (never 0 while the file has lines).
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionCountChanged)
    /// Number of dialogue lines (for the status bar).
    Q_PROPERTY(int lineCount READ lineCount NOTIFY lineCountChanged)
    Q_PROPERTY(bool hasFolds READ hasFolds NOTIFY foldsChanged)
    Q_PROPERTY(bool selectionHasFolds READ selectionHasFolds NOTIFY selectionHasFoldsChanged)
    Q_PROPERTY(int tagDisplayMode READ tagDisplayMode WRITE setTagDisplayMode NOTIFY tagDisplayModeChanged)
    Q_PROPERTY(QString hideOverridesChar READ hideOverridesChar WRITE setHideOverridesChar NOTIFY hideOverridesCharChanged)

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
        SelectedRole,
        DisplayTextRole,
        DocRowRole,
        ActiveRole,
        FoldRole,
        FoldDepthRole,
        FoldedHiddenCountRole,
        CpsRole,
    };
    Q_ENUM(Roles)
    enum FoldDisplay { NoFold, StartOpen, StartCollapsed, InsideFold, EndFold };

    enum TagDisplayMode { ShowTags, SimplifyTags, HideTags };
    Q_ENUM(TagDisplayMode)

    enum SortField {
        SortStart, SortEnd, SortStyle, SortActor, SortEffect, SortLayer, SortText,
        SortStrippedText,
    };
    Q_ENUM(SortField)

    SubtitleGridModel(AssFile *file, UndoManager *undoManager, QObject *parent = nullptr);
    void setVideoViewModel(VideoViewModel *video);
    void setCpsIgnoreOptions(bool whitespace, bool punctuation);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int activeRow() const;
    void setActiveRow(int row);

    int selectionCount() const;

    int lineCount() const;
    bool hasFolds() const;
    bool selectionHasFolds() const;
    Q_INVOKABLE int visibleRowForDocRow(int docRow) const;
    Q_INVOKABLE int docRowForVisibleRow(int visibleRow) const;
    Q_INVOKABLE void selectRangeVisible(int visibleRow);
    Q_INVOKABLE void ensureRowVisible(int docRow);
    void restoreActiveRow(int docRow);

    Q_INVOKABLE bool createFoldFromSelection();
    Q_INVOKABLE void toggleFoldsForSelection();
    Q_INVOKABLE void toggleFoldAtDocRow(int docRow);
    Q_INVOKABLE void clearFoldsForSelection();
    Q_INVOKABLE void openAllFolds();
    Q_INVOKABLE void closeAllFolds();
    Q_INVOKABLE void clearAllFolds();
    int tagDisplayMode() const { return tag_display_mode_; }
    void setTagDisplayMode(int mode);
    QString hideOverridesChar() const { return hide_overrides_char_; }
    void setHideOverridesChar(const QString &marker);

    /// The AssDialogue behind the selected row, or nullptr.
    AssDialogue *getActiveDialogue();

    /// The AssDialogue behind `row`, or nullptr.
    AssDialogue *dialogueAt(int row) const;

    /// Rows currently in the selection (only valid row indices).
    const QSet<int> &selectedRows() const;
    /// Ascending list of the selected rows.
    Q_INVOKABLE QList<int> sortedSelectedRows() const;
    void setAutomationSelection(const QList<int> &rows, int active);
    /// Select Lines may clear the selection without moving the active row.
    void setToolSelection(QSet<int> rows);

    // ---- Selection ---------------------------------------------------------

    /// Click: the only selected row.
    Q_INVOKABLE void selectSingle(int row);
    /// Ctrl+Click: add/remove `row`, keeping at least one row selected.
    Q_INVOKABLE void toggleSelect(int row);
    /// Shift+Click: everything between the anchor row and `row`.
    Q_INVOKABLE void selectRange(int row);
    /// Ctrl+A (when the EditBox does not have the keyboard focus).
    Q_INVOKABLE void selectAll();

    // ---- Row operations --------------------------------------------------

    Q_INVOKABLE void copySelectedRows();
    Q_INVOKABLE void cutSelectedRows();
    Q_INVOKABLE void pasteRows();
    /// True splits before the current frame; false splits after it.
    Q_INVOKABLE void splitAtVideoFrame(bool keepBefore);
    Q_INVOKABLE void joinSelectedRows(bool concatenate);
    Q_INVOKABLE void snapToVideo(bool start);
    Q_INVOKABLE void makeTimesContinuous(bool changeStart);
    Q_INVOKABLE void sortAllLines(int field);
    Q_INVOKABLE void sortSelectedLines(int field);
    Q_INVOKABLE void swapSelectedLines();
    Q_INVOKABLE void shiftSelectionToCurrentFrame();
    Q_INVOKABLE int insertAtVideoTime(bool after);
    /// Move one row up (negative) or down (positive).
    Q_INVOKABLE void moveSelectedRows(int delta);

    /// Rebuild the index table from the current AssFile contents and select the
    /// first line (used when a document is created or opened).
    Q_INVOKABLE void refresh();

    /// Tell views that the data of one row changed (after editing the
    /// AssDialogue behind it).
    Q_INVOKABLE void notifyRowUpdated(int row);

    /// Insert a new line before `row` (appends a default line when the file has
    /// no lines at all). Times are derived from the current line the same way
    /// subtitle/insert/before does it in the wx frontend.
    /// @return index of the new row, or -1 on failure
    Q_INVOKABLE int insertBefore(int row);
    /// Insert a new line after `row`, times derived like subtitle/insert/after.
    Q_INVOKABLE int insertAfter(int row);
    /// Deep-copy line `row` and insert the copy directly after it.
    Q_INVOKABLE int duplicateRow(int row);
    /// Deep-copy every selected line in one commit; the copies become the
    /// selection. @return number of lines copied
    Q_INVOKABLE int duplicateSelectedRows();
    /// Delete line `row`; when the last line is deleted a fresh empty line is
    /// added (the wx frontend never leaves a file without a dialogue line).
    Q_INVOKABLE void deleteRow(int row);
    /// Delete every selected line in one commit.
    /// @return number of lines deleted
    Q_INVOKABLE int deleteSelectedRows();

signals:
    void activeRowChanged();
    void selectionCountChanged();
    void lineCountChanged();
    void tagDisplayModeChanged();
    void hideOverridesCharChanged();
    void foldsChanged();
    void selectionHasFoldsChanged();

private:
    void notifyDisplayTextChanged();
    QString formatDisplayText(const QString &text) const;
    int tag_display_mode_ = ShowTags;
    QString hide_overrides_char_ = QStringLiteral("\u2600");
    void rebuildRows();
    void rebuildFolds();
    void refreshFoldVisibility();
    void emitActiveChanged(int previous);
    void moveActiveToVisibleFold();
    std::vector<int> selectedFoldIndices() const;
    void toggleFolds(const std::vector<int> &folds);
    void commitFoldChange(const char *description);
    int indexOf(const AssDialogue *line) const;
    /// Rebuild the row index but keep the selected row (clamped on shrink).
    void refreshPreservingActiveRow();

    /// Append a default line to an empty file and select it.
    int appendDefaultLine();
    bool copySelectionToClipboard() const;
    int deleteSelectedRows(const QString &description);
    QSet<AssDialogue *> selectedDialogues() const;
    void selectDialogues(const QSet<AssDialogue *> &selection, AssDialogue *active,
                         AssDialogue *anchor = nullptr);
    /// Commit a line-list change and leave the UndoManager to snapshot it.
    void commitRowChange(const QString &description, AssDialogue *line = nullptr);
    /// Select `line` and return its row index (-1 if it is not in the file).
    int selectLine(AssDialogue *line);
    /// Install a selection and make `active` the active row.
    void applySelection(const QSet<int> &selection, int active);
    /// Select exactly one row (or clear the selection when row < 0).
    void selectOnly(int row);
    /// Drop out-of-range rows; make sure at least the active row is selected.
    void sanitizeSelection();
    /// Tell the views that the selected state of the rows changed.
    void emitSelectionChanged();

    /// AssFile::AnnounceCommit slot: a commit changed the file.
    void onFileCommitted(int type, const AssDialogue *single_line);
    /// UndoManager::restored slot: a snapshot was swapped into the file, so
    /// every AssDialogue pointer in all_rows_ is dangling.
    void onRestored();

    AssFile *file_;
    UndoManager *undo_manager_;
    QPointer<VideoViewModel> video_vm_;
    std::vector<AssDialogue *> all_rows_;
    std::vector<int> visible_to_doc_;
    std::vector<int> doc_to_visible_;
    FoldState folds_;
    int active_row_ = -1;
    bool cps_ignore_whitespace_ = true;
    bool cps_ignore_punctuation_ = true;
    QSet<int> selected_rows_;
    /// Row a Shift+Click range starts from.
    int anchor_row_ = -1;
    agi::signal::Connection commit_connection_;
};
