// Must come before any Qt header: agi::signal has members named `slots`, and Qt
// defines `signals`/`slots` as macros (same ordering the wxWidgets frontend is
// immune to because it never mixes the two).
// OPT_GET/OPT_SET + config::opt (same header the wxWidgets preferences use).
#include "../../options.h"

#include "preferences_vm.h"

#include "../aegisub_core.h"

#include <libaegisub/exception.h>

#include <QDebug>

namespace {
constexpr const char *kAutoSaveOption = "App/Auto/Save";
}

PreferencesViewModel::PreferencesViewModel(QObject *parent)
: QObject(parent)
{
}

bool PreferencesViewModel::autoSave() const {
    if (!config::opt)
        return false;
    return OPT_GET(kAutoSaveOption)->GetBool();
}

void PreferencesViewModel::setAutoSave(bool value) {
    if (!config::opt)
        return;

    if (OPT_GET(kAutoSaveOption)->GetBool() == value)
        return;

    // Write into the real libaegisub option store: this is what the wxWidgets
    // frontend reads through the same config::opt object.
    OPT_SET(kAutoSaveOption)->SetBool(value);
    emit autoSaveChanged();

    QString error;
    if (!aegisub_qt::FlushOptions(&error))
        qWarning() << "[prefs] failed to persist" << kAutoSaveOption << ":" << error;
    else
        qInfo() << "[prefs]" << kAutoSaveOption << "=" << value << "->" << configFilePath();
}

QString PreferencesViewModel::configFilePath() const {
    return aegisub_qt::OptionsFilePath();
}
