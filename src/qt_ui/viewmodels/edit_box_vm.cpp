// Must come before any Qt header: agi::signal (via ass_file.h) has members
// named `slots`, and Qt defines `signals`/`slots` as macros.
#include "../../ass_file.h"

#include "edit_box_vm.h"
#include "subtitle_grid_model.h"

#include "../../ass_dialogue.h"

#include <libaegisub/ass/time.h>

#include <string>

namespace {

QString toQString(std::string const &s) {
    return QString::fromUtf8(s.data(), int(s.size()));
}

std::string toStdString(QString const &s) {
    const QByteArray utf8 = s.toUtf8();
    return std::string(utf8.constData(), size_t(utf8.size()));
}

} // namespace

EditBoxViewModel::EditBoxViewModel(SubtitleGridModel *model, QObject *parent)
: QObject(parent)
, model_(model)
{
    if (model_) {
        connect(model_, &SubtitleGridModel::activeRowChanged,
                this, &EditBoxViewModel::reload);
        // Opening a new file resets the model; make sure the editors reload
        // instead of showing a line from the previous document.
        connect(model_, &QAbstractItemModel::modelReset,
                this, &EditBoxViewModel::reload);
    }
}

AssDialogue *EditBoxViewModel::active() const {
    return model_ ? model_->getActiveDialogue() : nullptr;
}

void EditBoxViewModel::reload() {
    // Every property is derived from the active dialogue, so re-emit them all
    // and let QML reload the edit box.
    emit textChanged();
    emit startTimeChanged();
    emit endTimeChanged();
}

QString EditBoxViewModel::text() const {
    const AssDialogue *line = active();
    return line ? toQString(line->Text.get()) : QString();
}

void EditBoxViewModel::setText(const QString &value) {
    AssDialogue *line = active();
    if (!line)
        return;
    const std::string new_text = toStdString(value);
    if (line->Text.get() == new_text)
        return;
    line->Text = new_text;
    if (model_)
        model_->notifyRowUpdated(model_->activeRow());
    emit textChanged();
    emit edited();
}

QString EditBoxViewModel::startTime() const {
    const AssDialogue *line = active();
    return line ? QString::fromStdString(line->Start.GetAssFormatted()) : QString();
}

void EditBoxViewModel::setStartTime(const QString &value) {
    AssDialogue *line = active();
    if (!line)
        return;
    const agi::Time parsed(toStdString(value));
    if (int(line->Start) == int(parsed))
        return;
    line->Start = parsed;
    if (model_)
        model_->notifyRowUpdated(model_->activeRow());
    emit startTimeChanged();
    emit edited();
}

QString EditBoxViewModel::endTime() const {
    const AssDialogue *line = active();
    return line ? QString::fromStdString(line->End.GetAssFormatted()) : QString();
}

void EditBoxViewModel::setEndTime(const QString &value) {
    AssDialogue *line = active();
    if (!line)
        return;
    const agi::Time parsed(toStdString(value));
    if (int(line->End) == int(parsed))
        return;
    line->End = parsed;
    if (model_)
        model_->notifyRowUpdated(model_->activeRow());
    emit endTimeChanged();
    emit edited();
}
