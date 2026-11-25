#pragma once

#include <libaegisub/fs_fwd.h>
#include <boost/filesystem/path.hpp>
#include <QString>

namespace aegisub_qt {

inline agi::fs::path ToNativePath(QString const &path) {
#ifdef _WIN32
    return agi::fs::path(path.toStdWString());
#else
    return agi::fs::path(path.toStdString());
#endif
}

} // namespace aegisub_qt
