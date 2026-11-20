// Must come before any Qt header: agi::signal (via ass_file.h) has a member
// named `slots`, and Qt defines `signals`/`slots` as macros.
#include "../ass_file.h"

#include "aegisub_core.h"
#include "viewmodels/about_vm.h"
#include "viewmodels/app_info_vm.h"
#include "viewmodels/preferences_vm.h"
#include "viewmodels/properties_vm.h"
#include "viewmodels/translation_service.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDebug>

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("aegisub-qt");

    // Bring up the libaegisub core (paths + option store) before creating any
    // view model that reads configuration.
    QString core_error;
    const bool core_ok = aegisub_qt::InitializeCore(&core_error);
    if (!core_ok)
        qWarning() << "Aegisub core initialization failed:" << core_error;

    // The global in-memory subtitle project. LoadDefault() fills in the
    // standard [Script Info] header and the default [V4+ Styles] entry, using
    // the same defaults/options as the wxWidgets frontend (hence it needs the
    // option store to be up).
    AssFile script;
    if (core_ok)
        script.LoadDefault();

    TranslationService i18n;
    AppInfoViewModel appInfoVM;
    AboutViewModel aboutVM(&i18n);
    PreferencesViewModel preferencesVM;
    PropertiesViewModel propertiesVM(&script, &i18n);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("i18n", &i18n);
    engine.rootContext()->setContextProperty("appInfo", &appInfoVM);
    engine.rootContext()->setContextProperty("aboutVM", &aboutVM);
    engine.rootContext()->setContextProperty("preferencesVM", &preferencesVM);
    engine.rootContext()->setContextProperty("propertiesVM", &propertiesVM);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [] {
        QString error;
        if (!aegisub_qt::FlushOptions(&error))
            qWarning() << "Failed to flush options on exit:" << error;
    });

    return app.exec();
}
