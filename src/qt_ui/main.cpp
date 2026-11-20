// Must come before any Qt header: agi::signal (via ass_file.h) has a member
// named `slots`, and Qt defines `signals`/`slots` as macros.
#include "../ass_file.h"

#include "aegisub_core.h"
#include "viewmodels/about_vm.h"
#include "viewmodels/app_info_vm.h"
#include "viewmodels/edit_box_vm.h"
#include "viewmodels/preferences_vm.h"
#include "viewmodels/properties_vm.h"
#include "viewmodels/subtitle_grid_model.h"
#include "viewmodels/translation_service.h"

#include "../ass_dialogue.h"

#include <libaegisub/ass/time.h>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDebug>

#include <string>

namespace {

/// Insert a test dialogue line so the grid has something to show before any
/// file is loaded (the wx frontend gets these from an opened script instead).
void AddSampleLine(AssFile &file,
                   int layer, int start_ms, int end_ms,
                   std::string const &style, std::string const &actor,
                   std::string const &effect, std::string const &text,
                   bool comment = false) {
    auto *line = new AssDialogue;
    line->Comment = comment;
    line->Layer = layer;
    line->Start = agi::Time(start_ms);
    line->End = agi::Time(end_ms);
    line->Style = style;
    line->Actor = actor;
    line->Effect = effect;
    line->Text = text;
    file.Events.push_back(*line);
}

void FillSampleLines(AssFile &file) {
    // The default style created by LoadDefault() is called "Default".
    AddSampleLine(file, 0, 0, 2400, "Default", "Sora", "",
                  "{\\an8}第一条示例字幕 First sample line");
    AddSampleLine(file, 0, 2400, 5200, "Default", "Sora", "",
                  "第二条：QML 直接读取 AssFile 的 Events。");
    AddSampleLine(file, 1, 5200, 8100, "Default", "Narrator", "",
                  "{\\i1}Layer 1, italic narration{\\i0}");
    AddSampleLine(file, 0, 8100, 12000, "Default", "Sora", "",
                  "Dialogue line with a longer text that should be elided by the delegate "
                  "when the window is narrow enough to need it.");
    AddSampleLine(file, 0, 12000, 15000, "Default", "", "",
                  "Comment line example (not rendered on screen)", true);
}

} // namespace

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
    if (core_ok) {
        script.LoadDefault();
        FillSampleLines(script);
    }

    TranslationService i18n;
    AppInfoViewModel appInfoVM;
    AboutViewModel aboutVM(&i18n);
    PreferencesViewModel preferencesVM;
    PropertiesViewModel propertiesVM(&script, &i18n);
    SubtitleGridModel gridModel(&script);
    EditBoxViewModel editBoxVM(&gridModel);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("i18n", &i18n);
    engine.rootContext()->setContextProperty("appInfo", &appInfoVM);
    engine.rootContext()->setContextProperty("aboutVM", &aboutVM);
    engine.rootContext()->setContextProperty("preferencesVM", &preferencesVM);
    engine.rootContext()->setContextProperty("propertiesVM", &propertiesVM);
    engine.rootContext()->setContextProperty("gridModel", &gridModel);
    engine.rootContext()->setContextProperty("editBoxVM", &editBoxVM);
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
