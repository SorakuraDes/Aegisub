// Must come before any Qt header: agi::signal (via ass_file.h) has members
// named `slots`, and Qt defines `signals`/`slots` as macros.
#include "../../ass_file.h"

#include "edit_box_vm.h"

#include "../commit_bridge.h"
#include "subtitle_grid_model.h"
#include "undo_manager.h"

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

/// Keeps in_self_commit_ balanced even if Commit ever throws.
struct SelfCommitFlag {
    bool &flag;
    explicit SelfCommitFlag(bool &flag) : flag(flag) { flag = true; }
    ~SelfCommitFlag() { flag = false; }
};

/// How long consecutive edits of the same field keep folding into one undo step.
constexpr int kCoalesceWindowMs = 1000;

} // namespace

EditBoxViewModel::EditBoxViewModel(SubtitleGridModel *model, AssFile *file,
                                   UndoManager *undoManager, QObject *parent)
: QObject(parent)
, model_(model)
, file_(file)
, undo_manager_(undoManager)
, coalesce_timer_(this)
{
    coalesce_timer_.setSingleShot(true);
    coalesce_timer_.setInterval(kCoalesceWindowMs);
    connect(&coalesce_timer_, &QTimer::timeout, this, [this] { commit_id_ = -1; });

    if (model_) {
        connect(model_, &SubtitleGridModel::activeRowChanged, this, [this] {
            resetCoalescing();
            reload();
        });
        // Opening a new file resets the model; make sure the editors reload
        // instead of showing a line from the previous document.
        connect(model_, &QAbstractItemModel::modelReset, this, [this] {
            resetCoalescing();
            reload();
        });
    }

    if (file_)
        commit_connection_ = file_->AddCommitListener(&EditBoxViewModel::onFileCommitted, this);

    if (undo_manager_)
        connect(undo_manager_, &UndoManager::restored, this, &EditBoxViewModel::onRestored);
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

void EditBoxViewModel::resetCoalescing() {
    commit_id_ = -1;
    last_type_ = 0;
    last_row_ = -1;
    coalesce_timer_.stop();
}

void EditBoxViewModel::onFileCommitted(int type, const AssDialogue *) {
    if (in_self_commit_)
        return;

    // Structural changes (line inserted/deleted/reordered) invalidate the grid
    // model's row index. The grid rebuilds it in its own AnnounceCommit slot,
    // but agi::signal invokes slots in reverse connection order, so this slot
    // can run first -- reading the active line here would dereference a line
    // that deleteRow() already freed. The model reset the grid emits right
    // afterwards is what refreshes the editor instead.
    if (type == AssFile::COMMIT_NEW
        || (type & (AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_ORDER)) != 0)
        return;

    // Only commits that can change the active line's content matter here;
    // script-info/style edits must not reset the field the user is typing in.
    if ((type & AssFile::COMMIT_DIAG_FULL) == 0)
        return;

    resetCoalescing();
    reload();
    emit externalReload();
}

void EditBoxViewModel::refreshAfterExternalChange() {
    reload();
    emit externalReload();
}

void EditBoxViewModel::onRestored() {
    resetCoalescing();
    // The grid model rebuilds rows_ inside its own restored handler. Defer the
    // reload by one event loop iteration so it can never read a dangling line,
    // regardless of which order Qt happens to invoke the slots in.
    QMetaObject::invokeMethod(this, &EditBoxViewModel::refreshAfterExternalChange,
                              Qt::QueuedConnection);
}

void EditBoxViewModel::commitEdit(const QString &description, int type, AssDialogue *line) {
    if (!file_ || !line)
        return;

    // Switching line or field starts a new undo step even inside the timer
    // window; staying on the same field keeps amending the current one.
    const int row = model_ ? model_->activeRow() : -1;
    if (last_type_ != type || last_row_ != row)
        commit_id_ = -1;
    last_type_ = type;
    last_row_ = row;

    SelfCommitFlag guard(in_self_commit_);
    commit_id_ = aegisub_qt::commit(file_, toStdString(description), type, commit_id_, line);

    coalesce_timer_.start();
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
    commitEdit(QStringLiteral("Edit text"), AssFile::COMMIT_DIAG_TEXT, line);
    emit textChanged();
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
    commitEdit(QStringLiteral("Edit start time"), AssFile::COMMIT_DIAG_TIME, line);
    emit startTimeChanged();
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
    commitEdit(QStringLiteral("Edit end time"), AssFile::COMMIT_DIAG_TIME, line);
    emit endTimeChanged();
}
