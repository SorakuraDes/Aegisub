// ass_file.h must precede Qt headers because agi::signal uses Qt-reserved names.
#include "../../ass_file.h"

#include "select_lines_vm.h"
#include "subtitle_grid_model.h"
#include "../aegisub_core.h"
#include "../../ass_dialogue.h"
#include "../../options.h"

#include <QRegularExpression>
#include <QSet>

namespace {
QString valueFor(const AssDialogue &line, int field) {
    switch (field) {
    case SelectLinesViewModel::Style: return QString::fromStdString(line.Style.get());
    case SelectLinesViewModel::Actor: return QString::fromStdString(line.Actor.get());
    case SelectLinesViewModel::Effect: return QString::fromStdString(line.Effect.get());
    default: return QString::fromStdString(line.Text.get());
    }
}

int readIndex(const char *key, int last, int fallback) {
    const auto value = OPT_GET(key)->GetInt();
    return value >= 0 && value <= last ? static_cast<int>(value) : fallback;
}
} // namespace

SelectLinesViewModel::SelectLinesViewModel(AssFile *file, SubtitleGridModel *grid, QObject *parent)
: QObject(parent), file_(file), grid_(grid) {
    if (!config::opt) return;
    query_ = QString::fromStdString(OPT_GET("Tool/Select Lines/Text")->GetString());
    condition_ = readIndex("Tool/Select Lines/Condition", DoesNotMatch, Match);
    field_ = readIndex("Tool/Select Lines/Field", Effect, Text);
    selection_action_ = readIndex("Tool/Select Lines/Action", Intersect, Set);
    match_mode_ = readIndex("Tool/Select Lines/Mode", Regex, Contains);
    case_sensitive_ = OPT_GET("Tool/Select Lines/Match/Case")->GetBool();
    include_comments_ = OPT_GET("Tool/Select Lines/Match/Comment")->GetBool();
    include_dialogue_ = OPT_GET("Tool/Select Lines/Match/Dialogue")->GetBool() || !include_comments_;
}

void SelectLinesViewModel::setQuery(const QString &value) {
    if (query_ == value) return;
    query_ = value;
    emit queryChanged();
}

void SelectLinesViewModel::setCondition(int value) {
    if (value < Match || value > DoesNotMatch || condition_ == value) return;
    condition_ = value;
    emit conditionChanged();
}

void SelectLinesViewModel::setMatchMode(int value) {
    if (value < Exact || value > Regex || match_mode_ == value) return;
    match_mode_ = value;
    emit matchModeChanged();
}

void SelectLinesViewModel::setCaseSensitive(bool value) {
    if (case_sensitive_ == value) return;
    case_sensitive_ = value;
    emit caseSensitiveChanged();
}

void SelectLinesViewModel::setField(int value) {
    if (value < Text || value > Effect || field_ == value) return;
    field_ = value;
    emit fieldChanged();
}

void SelectLinesViewModel::setIncludeDialogue(bool value) {
    if (include_dialogue_ == value || (!value && !include_comments_)) return;
    include_dialogue_ = value;
    emit includeDialogueChanged();
}

void SelectLinesViewModel::setIncludeComments(bool value) {
    if (include_comments_ == value || (!value && !include_dialogue_)) return;
    include_comments_ = value;
    emit includeCommentsChanged();
}

void SelectLinesViewModel::setSelectionAction(int value) {
    if (value < Set || value > Intersect || selection_action_ == value) return;
    selection_action_ = value;
    emit selectionActionChanged();
}

void SelectLinesViewModel::setStatus(const QString &message, bool error, int matched, int selected) {
    status_message_ = message;
    status_error_ = error;
    matched_count_ = matched;
    selected_count_ = selected;
    emit statusChanged();
}

void SelectLinesViewModel::clearStatus() { setStatus({}, false); }

bool SelectLinesViewModel::applySelection() {
    if (!file_ || !grid_) {
        setStatus(QStringLiteral("No subtitle document"), true);
        return false;
    }
    QRegularExpression regex;
    if (match_mode_ == Regex) {
        regex = QRegularExpression(query_, case_sensitive_ ? QRegularExpression::NoPatternOption
                                                            : QRegularExpression::CaseInsensitiveOption);
        if (!regex.isValid()) {
            setStatus(QStringLiteral("Invalid regular expression"), true, 0, grid_->selectionCount());
            return false;
        }
    }

    const auto cs = case_sensitive_ ? Qt::CaseSensitive : Qt::CaseInsensitive;
    QSet<int> matches;
    int next_row = 0;
    // The grid indexes Events in document order. Read only the stored field;
    // the legacy matcher can write normalized text back into the event.
    for (const auto &line : file_->Events) {
        const int row = next_row++;
        if (line.Comment ? !include_comments_ : !include_dialogue_) continue;

        const QString value = valueFor(line, field_);
        bool matched;
        switch (match_mode_) {
        case Exact: matched = value.compare(query_, cs) == 0; break;
        case Regex: matched = regex.match(value).hasMatch(); break;
        default: matched = value.contains(query_, cs); break;
        }
        if (matched != (condition_ == DoesNotMatch)) matches.insert(row);
    }

    QSet<int> result = selection_action_ == Set ? matches : grid_->selectedRows();
    switch (selection_action_) {
    case Add: result.unite(matches); break;
    case Subtract: result.subtract(matches); break;
    case Intersect: result.intersect(matches); break;
    default: break;
    }
    grid_->setToolSelection(result);
    const int matched_count = static_cast<int>(matches.size());
    const int selected_count = grid_->selectionCount();
    setStatus(QStringLiteral("Matched %1 line(s); selected %2 line(s)").arg(matched_count).arg(selected_count),
              false, matched_count, selected_count);
    return true;
}

void SelectLinesViewModel::saveSettings() {
    if (!config::opt) return;
    OPT_SET("Tool/Select Lines/Text")->SetString(query_.toStdString());
    OPT_SET("Tool/Select Lines/Condition")->SetInt(condition_);
    OPT_SET("Tool/Select Lines/Field")->SetInt(field_);
    OPT_SET("Tool/Select Lines/Action")->SetInt(selection_action_);
    OPT_SET("Tool/Select Lines/Mode")->SetInt(match_mode_);
    OPT_SET("Tool/Select Lines/Match/Case")->SetBool(case_sensitive_);
    OPT_SET("Tool/Select Lines/Match/Dialogue")->SetBool(include_dialogue_);
    OPT_SET("Tool/Select Lines/Match/Comment")->SetBool(include_comments_);
    QString error;
    if (!aegisub_qt::FlushOptions(&error) && !error.isEmpty())
        setStatus(error, true, matched_count_, selected_count_);
}
