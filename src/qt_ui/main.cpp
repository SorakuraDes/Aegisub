// Must come before any Qt header: agi::signal (via ass_file.h) has a member
// named `slots`, and Qt defines `signals`/`slots` as macros.
#include "../ass_file.h"

#include "aegisub_core.h"
#include "viewmodels/about_vm.h"
#include "viewmodels/app_info_vm.h"
#include "viewmodels/document_vm.h"
#include "viewmodels/edit_box_vm.h"
#include "viewmodels/preferences_vm.h"
#include "viewmodels/properties_vm.h"
#include "viewmodels/subtitle_grid_model.h"
#include "viewmodels/translation_service.h"

#include <QCoreApplication>
#include <QFileInfo>
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

    // The document currently being edited. DocumentViewModel fills it with
    // either the default new-file content or a file passed on the command line.
    AssFile script;

    TranslationService i18n;
    AppInfoViewModel appInfoVM;
    AboutViewModel aboutVM(&i18n);
    PreferencesViewModel preferencesVM;
    PropertiesViewModel propertiesVM(&script, &i18n);
    SubtitleGridModel gridModel(&script);
    EditBoxViewModel editBoxVM(&gridModel);
    DocumentViewModel documentVM(&script, &gridModel);

    // Editing either the grid's edit box or the script properties must mark
    // the document as dirty (the title bar then shows " *").
    QObject::connect(&editBoxVM, &EditBoxViewModel::edited,
                     &documentVM, &DocumentViewModel::markDirty);
    QObject::connect(&propertiesVM, &PropertiesViewModel::edited,
                     &documentVM, &DocumentViewModel::markDirty);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("i18n", &i18n);
    engine.rootContext()->setContextProperty("appInfo", &appInfoVM);
    engine.rootContext()->setContextProperty("aboutVM", &aboutVM);
    engine.rootContext()->setContextProperty("preferencesVM", &preferencesVM);
    engine.rootContext()->setContextProperty("propertiesVM", &propertiesVM);
    engine.rootContext()->setContextProperty("gridModel", &gridModel);
    engine.rootContext()->setContextProperty("editBoxVM", &editBoxVM);
    engine.rootContext()->setContextProperty("documentVM", &documentVM);

    // Optional first command line argument: a subtitle file to open.
    const QStringList args = QCoreApplication::arguments();
    const QString startup_file = args.size() > 1 ? args.at(1) : QString();
    if (!startup_file.isEmpty() && QFileInfo(startup_file).isFile()) {
        if (!documentVM.openLocalPath(startup_file))
            qWarning() << "Failed to open" << startup_file << ":" << documentVM.lastError();
    } else {
        if (!documentVM.newDocument())
            qWarning() << "Failed to create a new document:" << documentVM.lastError();
    }

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
