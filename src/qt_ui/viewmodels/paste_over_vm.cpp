// The ASS headers must precede Qt headers because agi::signal uses `slots`.
#include "../../ass_file.h"
#include "../../ass_dialogue.h"
#include "../../options.h"

#include "../commit_bridge.h"
#include "paste_over_vm.h"
#include "subtitle_grid_model.h"

#include <libaegisub/exception.h>

#include <QClipboard>
#include <QGuiApplication>

#include <memory>
#include <string>
#include <typeinfo>
#include <utility>

namespace {

constexpr auto kFieldsOption = "Tool/Paste Lines Over/Fields";
enum Field { Comment, Layer, Start, End, Style, Actor, MarginL, MarginR, MarginV, Effect, Text, FieldCount };

std::unique_ptr<AssDialogue> parseLine(const QString &entry) {
    const auto raw = entry.toStdString();
    // A bare prefix has no separator byte for the core ASS parser to skip.
    if (raw != "Dialogue:" && raw != "Comment:") {
        try {
            return std::make_unique<AssDialogue>(raw);
        } catch (const agi::Exception &) {
            // Not an ASS dialogue, or malformed ASS.
        } catch (const std::bad_cast &) {
            // ASS numeric fields can throw boost::bad_lexical_cast.
        }
    }
    auto line = std::make_unique<AssDialogue>();
    line->End = 0;
    line->Text = raw;
    return line;
}

} // namespace

PasteOverViewModel::PasteOverViewModel(AssFile *file, SubtitleGridModel *grid, QObject *parent)
: QObject(parent)
, file_(file)
, grid_(grid)
, fields_(FieldCount, false) {
    // Don't silently retarget after a document edit, replacement, undo/redo,
    // or selection change. A new invocation takes a fresh clipboard snapshot.
    if (file_)
        commit_connection_ = file_->AddCommitListener([this](int, const AssDialogue *) { cancel(); });
    if (grid) {
        connect(grid, &SubtitleGridModel::activeRowChanged, this, &PasteOverViewModel::cancel);
        connect(grid, &SubtitleGridModel::selectionCountChanged, this, &PasteOverViewModel::cancel);
        connect(grid, &SubtitleGridModel::modelAboutToBeReset, this, &PasteOverViewModel::cancel);
        connect(grid, &QObject::destroyed, this, &PasteOverViewModel::cancel);
    }
}

QVariantList PasteOverViewModel::fields() const {
    QVariantList result;
    for (bool checked : fields_)
        result.push_back(checked);
    return result;
}

bool PasteOverViewModel::canAccept() const {
    return !entries_.isEmpty();
}

void PasteOverViewModel::setFields(std::vector<bool> fields) {
    fields.resize(FieldCount, false);
    if (fields_ == fields)
        return;
    fields_ = std::move(fields);
    emit fieldsChanged();
}

bool PasteOverViewModel::prepare() {
    cancel();
    if (!file_ || !grid_ || grid_->selectedRows().isEmpty() || !grid_->getActiveDialogue())
        return false;

    const auto *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return false;
    QString text = clipboard->text();
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    auto entries = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (entries.isEmpty())
        return false;
    for (auto &entry : entries)
        entry = entry.trimmed();

    if (config::opt) {
        setFields(OPT_GET(kFieldsOption)->GetListBool());
    } else {
        std::vector<bool> defaults(FieldCount, false);
        defaults[Effect] = true;
        setFields(std::move(defaults));
    }
    entries_ = std::move(entries);
    emit canAcceptChanged();
    return canAccept();
}

void PasteOverViewModel::setField(int index, bool checked) {
    if (!canAccept() || index < 0 || index >= FieldCount || fields_[index] == checked)
        return;
    fields_[index] = checked;
    emit fieldsChanged();
}

void PasteOverViewModel::selectAll() {
    if (canAccept())
        setFields(std::vector<bool>(FieldCount, true));
}

void PasteOverViewModel::selectNone() {
    if (canAccept())
        setFields(std::vector<bool>(FieldCount, false));
}

void PasteOverViewModel::selectTimes() {
    if (!canAccept())
        return;
    std::vector<bool> fields(FieldCount, false);
    fields[Start] = fields[End] = true;
    setFields(std::move(fields));
}

void PasteOverViewModel::selectText() {
    if (!canAccept())
        return;
    std::vector<bool> fields(FieldCount, false);
    fields[Text] = true;
    setFields(std::move(fields));
}

bool PasteOverViewModel::accept() {
    if (!canAccept() || !grid_ || !file_)
        return false;

    const auto selected = grid_->sortedSelectedRows();
    const int active = grid_->activeRow();
    std::vector<AssDialogue *> targets;
    if (selected.size() == 1) {
        for (int row = active; row >= 0 && row < grid_->lineCount() &&
             targets.size() < static_cast<size_t>(entries_.size()); ++row)
            targets.push_back(grid_->dialogueAt(row));
    } else {
        for (int row : selected) {
            if (targets.size() == static_cast<size_t>(entries_.size()))
                break;
            targets.push_back(grid_->dialogueAt(row));
        }
    }
    if (targets.empty()) {
        cancel();
        return false;
    }
    for (const auto *target : targets) {
        if (!target) {
            cancel();
            return false;
        }
    }

    std::vector<std::unique_ptr<AssDialogue>> parsed;
    parsed.reserve(targets.size());
    for (qsizetype i = 0; i < static_cast<qsizetype>(targets.size()); ++i)
        parsed.push_back(parseLine(entries_[i]));

    const auto fields = fields_;
    entries_.clear();
    for (size_t i = 0; i < targets.size(); ++i) {
        auto &target = *targets[i];
        const auto &source = *parsed[i];
        if (fields[Comment]) target.Comment = source.Comment;
        if (fields[Layer])   target.Layer = source.Layer;
        if (fields[Start])   target.Start = source.Start;
        if (fields[End])     target.End = source.End;
        if (fields[Style])   target.Style = source.Style;
        if (fields[Actor])   target.Actor = source.Actor;
        if (fields[MarginL]) target.Margin[0] = source.Margin[0];
        if (fields[MarginR]) target.Margin[1] = source.Margin[1];
        if (fields[MarginV]) target.Margin[2] = source.Margin[2];
        if (fields[Effect])  target.Effect = source.Effect;
        if (fields[Text])    target.Text = source.Text;
    }

    aegisub_qt::commit(file_, "paste over", AssFile::COMMIT_DIAG_FULL);
    if (config::opt)
        OPT_SET(kFieldsOption)->SetListBool(fields);
    emit canAcceptChanged();
    return true;
}

void PasteOverViewModel::cancel() {
    if (entries_.isEmpty())
        return;
    entries_.clear();
    emit canAcceptChanged();
}
