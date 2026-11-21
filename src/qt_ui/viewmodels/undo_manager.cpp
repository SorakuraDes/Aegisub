// Must come before any Qt header: agi::signal (via ass_file.h) has members
// named `slots`, and Qt defines `signals`/`slots` as macros.
#include "../../ass_file.h"

#include "undo_manager.h"

#include "../commit_bridge.h"

#include <utility>

UndoManager::UndoManager(AssFile *file, QObject *parent)
: QObject(parent)
, file_(file)
{
    if (file_) {
        push_state_connection_ = aegisub_qt::addUndoManager(
            file_,
            [this](const std::string &description, int *commit_id, AssDialogue *) {
                onPushState(description, commit_id);
            });
    }
    resetToClean();
}

UndoManager::~UndoManager() = default;

bool UndoManager::canUndo() const {
    return pos_ > 0;
}

bool UndoManager::canRedo() const {
    return pos_ + 1 < int(entries_.size());
}

QString UndoManager::undoDescription() const {
    if (!canUndo())
        return {};
    return entries_[size_t(pos_)].description;
}

QString UndoManager::redoDescription() const {
    if (!canRedo())
        return {};
    return entries_[size_t(pos_) + 1].description;
}

bool UndoManager::isModified() const {
    return pos_ != saved_pos_;
}

void UndoManager::undo() {
    if (canUndo())
        restoreTo(pos_ - 1);
}

void UndoManager::redo() {
    if (canRedo())
        restoreTo(pos_ + 1);
}

void UndoManager::resetToClean() {
    entries_.clear();

    Entry base;
    base.file = file_ ? makeSnapshot() : std::make_shared<AssFile>();
    // commit_id 0 is the sentinel for "no commit"; real ids start at 1, so a
    // stale amend id from a view model can never match the baseline.
    entries_.push_back(std::move(base));

    pos_ = 0;
    saved_pos_ = 0;
    next_commit_id_ = 1;
    coalesce_allowed_ = false;
    notifyAll();
}

void UndoManager::markSaved() {
    saved_pos_ = pos_;
    // Do not fold the next edit into the entry that now represents the saved
    // state: overwriting it would leave pos_ == saved_pos_ (i.e. "clean") while
    // the document actually differs from disk.
    coalesce_allowed_ = false;
    emit isModifiedChanged();
}

void UndoManager::onPushState(const std::string &description, int *commit_id) {
    if (!file_)
        return;

    const int amend_id = commit_id ? *commit_id : -1;

    // Coalescing: the caller passed back the id it got from its previous
    // Commit(), which is the id stored on the current top of the stack. Fold
    // the new state into that entry instead of pushing another undo step.
    if (amend_id >= 0 && coalesce_allowed_ && !entries_.empty()
        && pos_ == int(entries_.size()) - 1 && entries_[size_t(pos_)].commit_id == amend_id) {
        Entry &entry = entries_[size_t(pos_)];
        entry.file = makeSnapshot();
        if (!description.empty())
            entry.description = QString::fromUtf8(description.c_str(), int(description.size()));
        if (commit_id)
            *commit_id = amend_id;
        return;
    }

    // A genuinely new step drops the redo branch.
    while (int(entries_.size()) > pos_ + 1)
        entries_.pop_back();

    Entry entry;
    entry.file = makeSnapshot();
    entry.description = QString::fromUtf8(description.c_str(), int(description.size()));
    entry.commit_id = next_commit_id_++;
    entries_.push_back(std::move(entry));

    pos_ = int(entries_.size()) - 1;
    if (commit_id)
        *commit_id = entries_[size_t(pos_)].commit_id;

    coalesce_allowed_ = true;
    trimHistory();
    notifyAll();
}

void UndoManager::restoreTo(int pos) {
    if (!file_ || pos < 0 || pos >= int(entries_.size()))
        return;

    const Entry &entry = entries_[size_t(pos)];
    // Copy first: swap() steals the copy's contents (and a copy is what the
    // snapshot has to keep so that redo can apply it again).
    AssFile applied(*entry.file);
    // AssFile's copy constructor intentionally skips ProjectProperties.
    applied.Properties = entry.file->Properties;
    file_->swap(applied);

    pos_ = pos;
    coalesce_allowed_ = false;

    // Every AssDialogue pointer handed out before this point is dangling now.
    emit restored();
    notifyAll();
}

void UndoManager::trimHistory() {
    while (int(entries_.size()) > kMaxEntries) {
        entries_.pop_front();
        --pos_;
        if (saved_pos_ >= 0)
            --saved_pos_;
    }
}

std::shared_ptr<AssFile> UndoManager::makeSnapshot() const {
    auto snapshot = std::make_shared<AssFile>(*file_);
    snapshot->Properties = file_->Properties;
    return snapshot;
}

void UndoManager::notifyAll() {
    emit canUndoChanged();
    emit canRedoChanged();
    emit undoDescriptionChanged();
    emit redoDescriptionChanged();
    emit isModifiedChanged();
}
