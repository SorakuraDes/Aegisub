#pragma once

#include <QList>
#include <QObject>
#include <QString>

class AssDialogue;
class AssFile;
class SubtitleGridModel;

/// Search / replace over the dialogue lines, backing the non-modal search
/// window. All edits go through the shared commit bus, so replace operations
/// land on the normal undo stack.
class SearchViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(QString replacement READ replacement WRITE setReplacement NOTIFY replacementChanged)
    Q_PROPERTY(bool caseSensitive READ caseSensitive WRITE setCaseSensitive NOTIFY caseSensitiveChanged)
    Q_PROPERTY(bool useRegex READ useRegex WRITE setUseRegex NOTIFY useRegexChanged)
    /// 0 = Text, 1 = Actor, 2 = Effect, 3 = Style
    Q_PROPERTY(int searchField READ searchField WRITE setSearchField NOTIFY searchFieldChanged)
    Q_PROPERTY(bool inSelectionOnly READ inSelectionOnly WRITE setInSelectionOnly NOTIFY inSelectionOnlyChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    SearchViewModel(AssFile *file, SubtitleGridModel *gridModel, QObject *parent = nullptr);

    QString query() const;
    void setQuery(const QString &value);
    QString replacement() const;
    void setReplacement(const QString &value);
    bool caseSensitive() const;
    void setCaseSensitive(bool value);
    bool useRegex() const;
    void setUseRegex(bool value);
    int searchField() const;
    void setSearchField(int value);
    bool inSelectionOnly() const;
    void setInSelectionOnly(bool value);
    QString statusMessage() const;

    /// Find the next / previous match, wrapping around, and select + locate it.
    Q_INVOKABLE bool findNext();
    Q_INVOKABLE bool findPrev();
    /// Replace every occurrence in the active line, then jump to the next match.
    Q_INVOKABLE bool replaceCurrent();
    /// Replace every occurrence (in the selection when inSelectionOnly is set)
    /// with a single AssFile::Commit, so one undo reverts the whole operation.
    /// @return number of replaced occurrences
    Q_INVOKABLE int replaceAll();

signals:
    void queryChanged();
    void replacementChanged();
    void caseSensitiveChanged();
    void useRegexChanged();
    void searchFieldChanged();
    void inSelectionOnlyChanged();
    void statusMessageChanged();
    /// The grid should scroll this row into view.
    void locateRow(int row);

private:
    bool findFromActive(int direction);
    bool rowMatches(int row) const;
    bool hasMatch(const QString &text) const;
    QString replaceAllIn(const QString &text, int *occurrences) const;
    QString fieldValue(const AssDialogue *line) const;
    void setFieldValue(AssDialogue *line, const QString &value) const;
    /// Rows the search runs over: everything, or only the selection.
    QList<int> candidateRows() const;
    bool validateQuery();
    void setStatus(const QString &message);

    AssFile *file_;
    SubtitleGridModel *gridModel_;
    QString query_;
    QString replacement_;
    bool case_sensitive_ = false;
    bool use_regex_ = false;
    int search_field_ = 0;
    bool in_selection_only_ = false;
    QString status_;
};
