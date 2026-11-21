// Must come before any Qt header: agi::signal (via ass_file.h) has members
// named `slots`, and Qt defines `signals`/`slots` as macros.
#include "../../ass_file.h"

#include "search_vm.h"

#include "../commit_bridge.h"
#include "subtitle_grid_model.h"

#include "../../ass_dialogue.h"

#include <QRegularExpression>

#include <string>

namespace {

QString toQString(std::string const &s) {
    return QString::fromUtf8(s.data(), int(s.size()));
}

std::string toStdString(QString const &s) {
    const QByteArray utf8 = s.toUtf8();
    return std::string(utf8.constData(), size_t(utf8.size()));
}

Qt::CaseSensitivity caseSensitivity(bool case_sensitive) {
    return case_sensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
}

QRegularExpression::PatternOptions patternOptions(bool case_sensitive) {
    return case_sensitive ? QRegularExpression::NoPatternOption
                          : QRegularExpression::CaseInsensitiveOption;
}

} // namespace

SearchViewModel::SearchViewModel(AssFile *file, SubtitleGridModel *gridModel, QObject *parent)
: QObject(parent)
, file_(file)
, gridModel_(gridModel)
{
}

QString SearchViewModel::query() const {
    return query_;
}

void SearchViewModel::setQuery(const QString &value) {
    if (query_ == value)
        return;
    query_ = value;
    emit queryChanged();
}

QString SearchViewModel::replacement() const {
    return replacement_;
}

void SearchViewModel::setReplacement(const QString &value) {
    if (replacement_ == value)
        return;
    replacement_ = value;
    emit replacementChanged();
}

bool SearchViewModel::caseSensitive() const {
    return case_sensitive_;
}

void SearchViewModel::setCaseSensitive(bool value) {
    if (case_sensitive_ == value)
        return;
    case_sensitive_ = value;
    emit caseSensitiveChanged();
}

bool SearchViewModel::useRegex() const {
    return use_regex_;
}

void SearchViewModel::setUseRegex(bool value) {
    if (use_regex_ == value)
        return;
    use_regex_ = value;
    emit useRegexChanged();
}

int SearchViewModel::searchField() const {
    return search_field_;
}

void SearchViewModel::setSearchField(int value) {
    if (search_field_ == value)
        return;
    search_field_ = value;
    emit searchFieldChanged();
}

bool SearchViewModel::inSelectionOnly() const {
    return in_selection_only_;
}

void SearchViewModel::setInSelectionOnly(bool value) {
    if (in_selection_only_ == value)
        return;
    in_selection_only_ = value;
    emit inSelectionOnlyChanged();
}

QString SearchViewModel::statusMessage() const {
    return status_;
}

void SearchViewModel::setStatus(const QString &message) {
    if (status_ == message)
        return;
    status_ = message;
    emit statusMessageChanged();
}

QString SearchViewModel::fieldValue(const AssDialogue *line) const {
    if (!line)
        return {};
    switch (search_field_) {
        case 1:
            return toQString(line->Actor.get());
        case 2:
            return toQString(line->Effect.get());
        case 3:
            return toQString(line->Style.get());
        default:
            return toQString(line->Text.get());
    }
}

void SearchViewModel::setFieldValue(AssDialogue *line, const QString &value) const {
    if (!line)
        return;
    const std::string text = toStdString(value);
    switch (search_field_) {
        case 1:
            line->Actor = text;
            break;
        case 2:
            line->Effect = text;
            break;
        case 3:
            line->Style = text;
            break;
        default:
            line->Text = text;
            break;
    }
}

bool SearchViewModel::hasMatch(const QString &text) const {
    if (query_.isEmpty())
        return false;
    if (use_regex_) {
        const QRegularExpression re(query_, patternOptions(case_sensitive_));
        if (!re.isValid())
            return false;
        return re.match(text).hasMatch();
    }
    return text.contains(query_, caseSensitivity(case_sensitive_));
}

bool SearchViewModel::rowMatches(int row) const {
    if (!gridModel_)
        return false;
    return hasMatch(fieldValue(gridModel_->dialogueAt(row)));
}

QList<int> SearchViewModel::candidateRows() const {
    QList<int> rows;
    if (!gridModel_)
        return rows;
    if (in_selection_only_ && gridModel_->selectionCount() > 0)
        return gridModel_->sortedSelectedRows();

    const int count = gridModel_->rowCount();
    for (int i = 0; i < count; ++i)
        rows << i;
    return rows;
}

bool SearchViewModel::validateQuery() {
    if (query_.isEmpty()) {
        setStatus(QStringLiteral("Nothing to search for"));
        return false;
    }
    if (use_regex_ && !QRegularExpression(query_, patternOptions(case_sensitive_)).isValid()) {
        setStatus(QStringLiteral("Invalid regular expression"));
        return false;
    }
    return true;
}

bool SearchViewModel::findFromActive(int direction) {
    if (!gridModel_ || !validateQuery())
        return false;

    const QList<int> rows = candidateRows();
    if (rows.isEmpty()) {
        setStatus(QStringLiteral("No lines to search"));
        return false;
    }

    const int active = gridModel_->activeRow();
    // Index of the active row in the candidate list, or before its start so the
    // first candidate is examined first when it is not part of the search set.
    int start = direction > 0 ? -1 : 0;
    for (int i = 0; i < rows.size(); ++i) {
        if (rows[i] == active) {
            start = i;
            break;
        }
    }

    const int count = int(rows.size());
    for (int step = 1; step <= count; ++step) {
        const int index = ((start + direction * step) % count + count) % count;
        const int row = rows[index];
        if (rowMatches(row)) {
            gridModel_->selectSingle(row);
            emit locateRow(row);
            setStatus(QStringLiteral("Line %1").arg(row + 1));
            return true;
        }
    }

    setStatus(QStringLiteral("No matches"));
    return false;
}

bool SearchViewModel::findNext() {
    return findFromActive(1);
}

bool SearchViewModel::findPrev() {
    return findFromActive(-1);
}

QString SearchViewModel::replaceAllIn(const QString &text, int *occurrences) const {
    *occurrences = 0;
    if (query_.isEmpty())
        return text;

    if (use_regex_) {
        const QRegularExpression re(query_, patternOptions(case_sensitive_));
        if (!re.isValid())
            return text;

        int count = 0;
        auto iterator = re.globalMatch(text);
        while (iterator.hasNext()) {
            iterator.next();
            ++count;
        }
        if (count == 0)
            return text;

        QString result = text;
        result.replace(re, replacement_);
        *occurrences = count;
        return result;
    }

    QString result = text;
    int position = 0;
    int count = 0;
    const Qt::CaseSensitivity sensitivity = caseSensitivity(case_sensitive_);
    while (position <= result.size()) {
        const int index = result.indexOf(query_, position, sensitivity);
        if (index < 0)
            break;
        result.replace(index, query_.size(), replacement_);
        ++count;
        // Skip past the inserted text so a replacement containing the query
        // cannot loop forever.
        position = index + replacement_.size();
    }

    *occurrences = count;
    return result;
}

bool SearchViewModel::replaceCurrent() {
    if (!file_ || !gridModel_ || !validateQuery())
        return false;

    int row = gridModel_->activeRow();
    AssDialogue *line = gridModel_->dialogueAt(row);
    if (!line || !rowMatches(row)) {
        // Nothing on the active line: jump to the next match first.
        if (!findFromActive(1))
            return false;
        row = gridModel_->activeRow();
        line = gridModel_->dialogueAt(row);
        if (!line)
            return false;
    }

    int replaced = 0;
    const QString updated = replaceAllIn(fieldValue(line), &replaced);
    if (replaced == 0)
        return false;

    setFieldValue(line, updated);
    const int type = search_field_ == 0 ? AssFile::COMMIT_DIAG_TEXT : AssFile::COMMIT_DIAG_META;
    aegisub_qt::commit(file_, "Replace", type, -1, line);

    setStatus(QStringLiteral("Replaced %1 occurrence(s)").arg(replaced));
    findFromActive(1);
    return true;
}

int SearchViewModel::replaceAll() {
    if (!file_ || !gridModel_ || !validateQuery())
        return 0;

    const QList<int> rows = candidateRows();
    int replaced_lines = 0;
    int replaced_occurrences = 0;

    for (int row : rows) {
        AssDialogue *line = gridModel_->dialogueAt(row);
        if (!line)
            continue;

        int count = 0;
        const QString updated = replaceAllIn(fieldValue(line), &count);
        if (count == 0)
            continue;

        setFieldValue(line, updated);
        ++replaced_lines;
        replaced_occurrences += count;
    }

    if (replaced_occurrences == 0) {
        setStatus(QStringLiteral("No matches"));
        return 0;
    }

    // One commit for every changed line: a single undo reverts the whole batch.
    const int type = search_field_ == 0 ? AssFile::COMMIT_DIAG_TEXT : AssFile::COMMIT_DIAG_META;
    aegisub_qt::commit(file_, "Replace all", type, -1, nullptr);

    setStatus(QStringLiteral("Replaced %1 occurrence(s) in %2 line(s)")
                  .arg(replaced_occurrences)
                  .arg(replaced_lines));
    return replaced_occurrences;
}
