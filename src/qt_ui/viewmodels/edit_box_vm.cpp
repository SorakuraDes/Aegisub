// Must come before any Qt header: agi::signal (via ass_file.h) has members
// named `slots`, and Qt defines `signals`/`slots` as macros.
#include "../../ass_file.h"
#include "../../options.h"
#include <libaegisub/character_count.h>

#include "edit_box_vm.h"

#include "../commit_bridge.h"
#include "subtitle_grid_model.h"
#include "undo_manager.h"

#include "../../ass_dialogue.h"
#include "../../ass_style.h"

#include <libaegisub/ass/time.h>

#include <algorithm>
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

/// Coalescing groups. Metadata fields all commit COMMIT_DIAG_META, so they need
/// their own key to avoid merging two different fields into one undo step.
enum Field : int {
    kFieldText = 1,
    kFieldStart,
    kFieldEnd,
    kFieldDuration,
    kFieldComment,
    kFieldLayer,
    kFieldStyle,
    kFieldActor,
    kFieldEffect,
};

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
            refreshAvailableStyles();
            reload();
        });
    }

    if (file_)
        commit_connection_ = file_->AddCommitListener(&EditBoxViewModel::onFileCommitted, this);

    if (undo_manager_)
        connect(undo_manager_, &UndoManager::restored, this, &EditBoxViewModel::onRestored);

    refreshAvailableStyles();
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
    emit durationChanged();
    emit commentChanged();
    emit layerChanged();
    emit styleChanged();
    emit actorChanged();
    emit effectChanged();
}

void EditBoxViewModel::resetCoalescing() {
    commit_id_ = -1;
    last_type_ = 0;
    last_row_ = -1;
    last_field_ = 0;
    coalesce_timer_.stop();
}

void EditBoxViewModel::refreshAvailableStyles() {
    QStringList styles;
    if (file_) {
        for (auto const &style : file_->Styles) {
            const QString name = QString::fromStdString(style.name);
            if (!name.isEmpty() && !styles.contains(name))
                styles << name;
        }
    }
    if (styles.isEmpty())
        styles << QStringLiteral("Default");

    if (styles == available_styles_)
        return;
    available_styles_ = styles;
    emit availableStylesChanged();
}

QStringList EditBoxViewModel::availableStyles() const {
    return available_styles_;
}

void EditBoxViewModel::onFileCommitted(int type, const AssDialogue *) {
    if (in_self_commit_)
        return;

    // Style names may have changed even though the line list did not.
    if (type == AssFile::COMMIT_NEW || (type & AssFile::COMMIT_STYLES))
        refreshAvailableStyles();

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
    // script-info edits must not reset the field the user is typing in.
    if ((type & AssFile::COMMIT_DIAG_FULL) == 0)
        return;

    resetCoalescing();
    reload();
    emit externalReload();
}

void EditBoxViewModel::refreshAfterExternalChange() {
    refreshAvailableStyles();
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

void EditBoxViewModel::commitEdit(const QString &description, int type, AssDialogue *line,
                                  int field_key) {
    if (!file_)
        return;

    // Switching line or field starts a new undo step even inside the timer
    // window; staying on the same field keeps amending the current one.
    const int row = model_ ? model_->activeRow() : -1;
    if (last_type_ != type || last_row_ != row || last_field_ != field_key)
        commit_id_ = -1;
    last_type_ = type;
    last_row_ = row;
    last_field_ = field_key;

    SelfCommitFlag guard(in_self_commit_);
    commit_id_ = aegisub_qt::commit(file_, toStdString(description), type, commit_id_, line);

    coalesce_timer_.start();
}

QList<AssDialogue *> EditBoxViewModel::metadataTargets() const {
    QList<AssDialogue *> targets;
    if (model_ && file_ && model_->selectionCount() > 1) {
        for (int row : model_->sortedSelectedRows()) {
            if (auto *line = model_->dialogueAt(row))
                targets << line;
        }
    }
    if (targets.isEmpty()) {
        if (auto *line = active())
            targets << line;
    }
    return targets;
}

void EditBoxViewModel::commitMetadata(const QString &description, int field_key,
                                      const QList<AssDialogue *> &targets) {
    if (targets.isEmpty())
        return;
    // A batch edit is a single commit with no single_line, so the grid rebuilds
    // and every selected row repaints; one Ctrl+Z undoes all of them.
    commitEdit(description, AssFile::COMMIT_DIAG_META,
               targets.size() > 1 ? nullptr : targets.first(), field_key);
}

void EditBoxViewModel::commitAndNextLine() {
    if (!model_)
        return;

    // Do not fold the next line's first edit into this one.
    resetCoalescing();

    const int row = model_->activeRow();
    if (row + 1 < model_->rowCount())
        model_->setActiveRow(row + 1);
    else
        model_->insertAfter(row);
}

void EditBoxViewModel::handleEnterKey(bool shift) {
    if (shift) {
        emit lineBreakRequested();
        return;
    }

    commitAndNextLine();
    emit bodyEditorFocusRequested();
}

// ---------------------------------------------------------------------------
// Body text
// ---------------------------------------------------------------------------

int EditBoxViewModel::longestVisibleLineLength(const QString &value) const {
    int flags = agi::IGNORE_BLOCKS;
    if (config::opt && OPT_GET("Subtitle/Character Counter/Ignore Whitespace")->GetBool()) flags |= agi::IGNORE_WHITESPACE;
    if (config::opt && OPT_GET("Subtitle/Character Counter/Ignore Punctuation")->GetBool()) flags |= agi::IGNORE_PUNCTUATION;
    return int(agi::MaxLineLength(toStdString(value), flags));
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
    commitEdit(QStringLiteral("Edit text"), AssFile::COMMIT_DIAG_TEXT, line, kFieldText);
    emit textChanged();
}

// ---------------------------------------------------------------------------
// Times
// ---------------------------------------------------------------------------

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
    commitEdit(QStringLiteral("Edit start time"), AssFile::COMMIT_DIAG_TIME, line, kFieldStart);
    emit startTimeChanged();
    emit durationChanged();
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
    commitEdit(QStringLiteral("Edit end time"), AssFile::COMMIT_DIAG_TIME, line, kFieldEnd);
    emit endTimeChanged();
    emit durationChanged();
}

QString EditBoxViewModel::duration() const {
    const AssDialogue *line = active();
    if (!line)
        return {};
    const int ms = std::max(0, int(line->End) - int(line->Start));
    return QString::fromStdString(agi::Time(ms).GetAssFormatted());
}

void EditBoxViewModel::setDuration(const QString &value) {
    AssDialogue *line = active();
    if (!line)
        return;

    const agi::Time parsed(toStdString(value));
    const int ms = std::max(0, int(parsed));
    if (std::max(0, int(line->End) - int(line->Start)) == ms)
        return;

    line->End = agi::Time(int(line->Start) + ms);
    commitEdit(QStringLiteral("Edit duration"), AssFile::COMMIT_DIAG_TIME, line, kFieldDuration);
    emit endTimeChanged();
    emit durationChanged();
}

// ---------------------------------------------------------------------------
// Metadata
// ---------------------------------------------------------------------------

bool EditBoxViewModel::comment() const {
    const AssDialogue *line = active();
    return line ? line->Comment : false;
}

void EditBoxViewModel::setComment(bool value) {
    const QList<AssDialogue *> targets = metadataTargets();
    bool changed = false;
    for (auto *line : targets) {
        if (line->Comment != value) {
            line->Comment = value;
            changed = true;
        }
    }
    if (!changed)
        return;
    commitMetadata(QStringLiteral("Toggle comment"), kFieldComment, targets);
    emit commentChanged();
}

int EditBoxViewModel::layer() const {
    const AssDialogue *line = active();
    return line ? line->Layer : 0;
}

void EditBoxViewModel::setLayer(int value) {
    const QList<AssDialogue *> targets = metadataTargets();
    const int clamped = std::max(0, value);
    bool changed = false;
    for (auto *line : targets) {
        if (line->Layer != clamped) {
            line->Layer = clamped;
            changed = true;
        }
    }
    if (!changed)
        return;
    commitMetadata(QStringLiteral("Edit layer"), kFieldLayer, targets);
    emit layerChanged();
}

QString EditBoxViewModel::style() const {
    const AssDialogue *line = active();
    return line ? toQString(line->Style.get()) : QString();
}

void EditBoxViewModel::setStyle(const QString &value) {
    const QList<AssDialogue *> targets = metadataTargets();
    const std::string new_style = toStdString(value);
    bool changed = false;
    for (auto *line : targets) {
        if (line->Style.get() != new_style) {
            line->Style = new_style;
            changed = true;
        }
    }
    if (!changed)
        return;
    commitMetadata(QStringLiteral("Edit style"), kFieldStyle, targets);
    emit styleChanged();
}

QString EditBoxViewModel::actor() const {
    const AssDialogue *line = active();
    return line ? toQString(line->Actor.get()) : QString();
}

void EditBoxViewModel::setActor(const QString &value) {
    const QList<AssDialogue *> targets = metadataTargets();
    const std::string new_actor = toStdString(value);
    bool changed = false;
    for (auto *line : targets) {
        if (line->Actor.get() != new_actor) {
            line->Actor = new_actor;
            changed = true;
        }
    }
    if (!changed)
        return;
    commitMetadata(QStringLiteral("Edit actor"), kFieldActor, targets);
    emit actorChanged();
}

QString EditBoxViewModel::effect() const {
    const AssDialogue *line = active();
    return line ? toQString(line->Effect.get()) : QString();
}

void EditBoxViewModel::setEffect(const QString &value) {
    const QList<AssDialogue *> targets = metadataTargets();
    const std::string new_effect = toStdString(value);
    bool changed = false;
    for (auto *line : targets) {
        if (line->Effect.get() != new_effect) {
            line->Effect = new_effect;
            changed = true;
        }
    }
    if (!changed)
        return;
    commitMetadata(QStringLiteral("Edit effect"), kFieldEffect, targets);
    emit effectChanged();
}
