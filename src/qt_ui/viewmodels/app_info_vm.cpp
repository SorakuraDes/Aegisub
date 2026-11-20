#include "app_info_vm.h"

// Aegisub's own version API (src/version.h + src/version.cpp), which in turn
// reads the generated build/git_version.h. Reusing the very same translation
// unit the wxWidgets build uses keeps the two frontends in sync.
#include "../../version.h"

// libaegisub is linked into aegisub-qt; agi::format lives in the core library
// (libaegisub/common/format.cpp), so this call also exercises the libaegisub
// -> boost/iconv dependency chain at link time.
#include <libaegisub/format.h>

#include <string>

AppInfoViewModel::AppInfoViewModel(QObject *parent)
: QObject(parent)
, version_(QString::fromStdString(agi::format("%s", std::string(GetVersionNumber()))))
{
}

QString AppInfoViewModel::version() const {
    return version_;
}
