/// @file commit_bridge.cpp
/// @brief wx-side implementation of commit_bridge.h (compiled into ass_qt_core).
///
/// This file relies on the wx precompiled header (src/include/agi_pre.h) for
/// wxString; it must not include any Qt header.

#include "commit_bridge.h"

#include "../ass_file.h"

#include <memory>
#include <utility>

namespace {

/// Keeps the conversion + callback alive for as long as the AssFile keeps the
/// slot connected. The slot lambda captures a shared_ptr, so the AssFile owns
/// the relay and dropping the agi connection releases it again.
class PushStateRelay {
public:
    explicit PushStateRelay(aegisub_qt::PushStateCallback callback)
    : callback_(std::move(callback))
    {
    }

    void onPushState(AssFileCommit commit) {
        // AssFileCommit only borrows the description, so convert it right away.
        callback_(commit.message.ToStdString(wxConvUTF8), commit.commit_id, commit.single_line);
    }

private:
    aegisub_qt::PushStateCallback callback_;
};

} // namespace

namespace aegisub_qt {

agi::signal::Connection addUndoManager(AssFile *file, PushStateCallback callback) {
    auto relay = std::make_shared<PushStateRelay>(std::move(callback));
    return agi::signal::Connection(
        file->AddUndoManager([relay](AssFileCommit commit) { relay->onPushState(commit); }));
}

int commit(AssFile *file, const std::string &description, int type, int amend_id,
           AssDialogue *single_line) {
    return file->Commit(wxString::FromUTF8(description.data(), description.size()), type, amend_id,
                        single_line);
}

} // namespace aegisub_qt
