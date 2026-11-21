#pragma once

/// @file commit_bridge.h
/// @brief Tiny wx-free bridge to AssFile::Commit / AssFile::AddUndoManager.
///
/// ass_file.h declares Commit() in terms of wxString and PushState in terms of
/// a struct holding a wxString reference. Pulling <wx/string.h> into a Qt
/// translation unit is asking for trouble, so this bridge (compiled into
/// ass_qt_core, which already uses the wx precompiled header) does the wxString
/// conversion and Qt-facing code only ever sees UTF-8 std::string.
///
/// Nothing in this header depends on Qt, so it is safe to include from both
/// sides of the build.

#include <libaegisub/signal.h>

#include <functional>
#include <string>

class AssDialogue;
class AssFile;

namespace aegisub_qt {

/// Signature of AssFile::PushState once the description has been converted to
/// UTF-8. `commit_id` points at the amend id passed to Commit(); the callee
/// writes back the id the new state was actually stored under (coalescing
/// reuses an existing id instead of allocating a new one).
using PushStateCallback =
    std::function<void(const std::string &description, int *commit_id, AssDialogue *single_line)>;

/// Subscribe to AssFile::PushState without exposing wxString to Qt code.
///
/// The returned connection must be kept alive for as long as the callback
/// should fire; both the signal slot and the callback are owned by the
/// returned object.
agi::signal::Connection addUndoManager(AssFile *file, PushStateCallback callback);

/// AssFile::Commit wrapper taking a UTF-8 description instead of wxString.
/// Returns the commit id the undo stack stored the state under.
int commit(AssFile *file, const std::string &description, int type, int amend_id = -1,
           AssDialogue *single_line = nullptr);

} // namespace aegisub_qt
