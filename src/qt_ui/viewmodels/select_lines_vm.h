#pragma once

#include <QObject>
#include <QString>

class AssFile;
class SubtitleGridModel;

class SelectLinesViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(int condition READ condition WRITE setCondition NOTIFY conditionChanged)
    Q_PROPERTY(int matchMode READ matchMode WRITE setMatchMode NOTIFY matchModeChanged)
    Q_PROPERTY(bool caseSensitive READ caseSensitive WRITE setCaseSensitive NOTIFY caseSensitiveChanged)
    Q_PROPERTY(int field READ field WRITE setField NOTIFY fieldChanged)
    Q_PROPERTY(bool includeDialogue READ includeDialogue WRITE setIncludeDialogue NOTIFY includeDialogueChanged)
    Q_PROPERTY(bool includeComments READ includeComments WRITE setIncludeComments NOTIFY includeCommentsChanged)
    Q_PROPERTY(int selectionAction READ selectionAction WRITE setSelectionAction NOTIFY selectionActionChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(bool statusError READ statusError NOTIFY statusChanged)
    Q_PROPERTY(int matchedCount READ matchedCount NOTIFY statusChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY statusChanged)

public:
    // Numeric values are shared with the legacy Tool/Select Lines options.
    enum Condition { Match, DoesNotMatch };
    enum MatchMode { Exact, Contains, Regex };
    enum Field { Text, Style, Actor, Effect };
    enum SelectionAction { Set, Add, Subtract, Intersect };
    Q_ENUM(Condition)
    Q_ENUM(MatchMode)
    Q_ENUM(Field)
    Q_ENUM(SelectionAction)

    SelectLinesViewModel(AssFile *file, SubtitleGridModel *grid, QObject *parent = nullptr);

    QString query() const { return query_; }
    int condition() const { return condition_; }
    int matchMode() const { return match_mode_; }
    bool caseSensitive() const { return case_sensitive_; }
    int field() const { return field_; }
    bool includeDialogue() const { return include_dialogue_; }
    bool includeComments() const { return include_comments_; }
    int selectionAction() const { return selection_action_; }
    QString statusMessage() const { return status_message_; }
    bool statusError() const { return status_error_; }
    int matchedCount() const { return matched_count_; }
    int selectedCount() const { return selected_count_; }

    void setQuery(const QString &value);
    void setCondition(int value);
    void setMatchMode(int value);
    void setCaseSensitive(bool value);
    void setField(int value);
    void setIncludeDialogue(bool value);
    void setIncludeComments(bool value);
    void setSelectionAction(int value);

    /// Apply to the grid selection only; an invalid query leaves it unchanged.
    Q_INVOKABLE bool applySelection();
    Q_INVOKABLE void saveSettings();
    Q_INVOKABLE void clearStatus();

signals:
    void queryChanged();
    void conditionChanged();
    void matchModeChanged();
    void caseSensitiveChanged();
    void fieldChanged();
    void includeDialogueChanged();
    void includeCommentsChanged();
    void selectionActionChanged();
    void statusChanged();

private:
    void setStatus(const QString &message, bool error, int matched = 0, int selected = 0);

    const AssFile *file_;
    SubtitleGridModel *grid_;
    QString query_;
    int condition_ = Match;
    int match_mode_ = Contains;
    bool case_sensitive_ = false;
    int field_ = Text;
    bool include_dialogue_ = true;
    bool include_comments_ = false;
    int selection_action_ = Set;
    QString status_message_;
    bool status_error_ = false;
    int matched_count_ = 0;
    int selected_count_ = 0;
};
