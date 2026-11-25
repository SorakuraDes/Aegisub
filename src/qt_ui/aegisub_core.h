#pragma once

#include <QString>

namespace aegisub_qt {

/// Initialize the libaegisub subsystems the Qt UI needs: logging dispatch,
/// agi::Path and the agi::Options store (the same config.json the wxWidgets
/// frontend uses).
/// Returns false and fills `error` when the core cannot be initialized.
bool InitializeCore(QString *error = nullptr);

/// Persist the current agi::Options values to disk (agi::Options::Flush).
bool FlushOptions(QString *error = nullptr);

/// Absolute path of the config.json currently written to.
QString OptionsFilePath();

/// Read the current log only if the buffer has changed since revision.
bool ReadRecentLog(quint64 &revision, QString &text);
void ClearRecentLog();

} // namespace aegisub_qt
