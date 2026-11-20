// Must come before any Qt header: agi::signal has members named `slots`, and Qt
// defines `signals`/`slots` as macros.
// src/options.h holds the config::{opt,path,...} globals and the OPT_GET/OPT_SET
// macros; it is the same header preferences.cpp uses in the wxWidgets frontend.
#include "../options.h"

#include "aegisub_core.h"

#include <libaegisub/dispatch.h>
#include <libaegisub/exception.h>
#include <libaegisub/fs.h>
#include <libaegisub/log.h>
#include <libaegisub/make_unique.h>
#include <libaegisub/option.h>
#include <libaegisub/path.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <exception>
#include <sstream>
#include <string>

// The wxWidgets frontend defines these in src/main.cpp. The Qt frontend is a
// separate executable, so it provides its own definitions here.
namespace config {
agi::Options *opt = nullptr;
agi::MRUManager *mru = nullptr;
agi::Path *path = nullptr;
Automation4::AutoloadScriptManager *global_scripts = nullptr;
}

namespace {

const char *kTraceFile = "aegisub_qt_core.log";

/// Optional startup tracing next to the executable (enabled with
/// AEGISUB_QT_TRACE=1); a crash before the QML engine comes up would otherwise
/// leave no trace at all.
void trace(const QString &message) {
    if (qEnvironmentVariableIsEmpty("AEGISUB_QT_TRACE"))
        return;
    QFile file(QDir(QCoreApplication::applicationDirPath()).filePath(QString::fromLatin1(kTraceFile)));
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        file.write(message.toUtf8());
        file.write("\n");
        file.flush();
    }
}

std::string readTextFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = file.readAll();
    return std::string(data.constData(), size_t(data.size()));
}

QString toQString(agi::fs::path const &path) {
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

} // namespace

namespace aegisub_qt {

bool InitializeCore(QString *error) {
    auto fail = [error](const QString &message) {
        trace(QStringLiteral("FAIL: ") + message);
        if (error)
            *error = message;
        return false;
    };

    if (config::opt && config::path)
        return true;

    try {
        trace(QStringLiteral("step: dispatch/log"));
        // libaegisub's logging is used all over the core; without a sink every
        // LOG_* macro would dereference a null pointer.
        agi::dispatch::Init([](agi::dispatch::Thunk f) { f(); });
        if (!agi::log::log) {
            agi::log::log = new agi::log::LogSink;
            agi::log::log->Subscribe(agi::make_unique<agi::log::EmitSTDOUT>());
        }

        trace(QStringLiteral("step: path"));
        const QDir app_dir(QCoreApplication::applicationDirPath());
        if (!config::path) {
            config::path = new agi::Path;

            // Resolve the user config directory:
            //   1. AEGISUB_QT_CONFIG_DIR wins (sandboxed/test runs),
            //   2. otherwise the original ?user (%APPDATA%\Aegisub),
            //   3. if that is not writable, fall back to .qt_user next to the exe
            //      so the prototype can still exercise the real option store.
            QString user_dir;
            const QByteArray override_dir = qgetenv("AEGISUB_QT_CONFIG_DIR");
            if (!override_dir.isEmpty()) {
                user_dir = QString::fromLocal8Bit(override_dir);
            } else {
                user_dir = toQString(config::path->Decode("?user"));
            }

            bool created = QDir().mkpath(user_dir);
            if (!created && QFileInfo(user_dir).isDir())
                created = true;
            if (!created && override_dir.isEmpty()) {
                const QString fallback = app_dir.filePath(QStringLiteral(".qt_user"));
                if (QDir().mkpath(fallback)) {
                    trace(QStringLiteral("  ?user not writable (") + user_dir
                          + QStringLiteral("), falling back to ") + fallback);
                    user_dir = fallback;
                    created = true;
                }
            }
            trace(QStringLiteral("  ?user = ") + user_dir);
            if (!created)
                return fail(QStringLiteral("Could not create the user config directory: ") + user_dir);

            config::path->SetToken("?user", agi::fs::path(user_dir.toStdWString()));
        }

        const std::string defaults = readTextFile(app_dir.filePath(QStringLiteral("default_config.json")));
        if (defaults.empty())
            return fail(QStringLiteral("Missing default_config.json next to the executable"));
        const std::string platform_defaults = readTextFile(app_dir.filePath(QStringLiteral("default_config_platform.json")));
        trace(QStringLiteral("step: defaults read (%1 bytes)").arg(qulonglong(defaults.size())));

        // Same construction order as src/main.cpp: default config, then the
        // platform config, then the user config.
        trace(QStringLiteral("step: options"));
        config::opt = new agi::Options(config::path->Decode("?user/config.json"),
                                       { defaults.data(), defaults.size() });

        if (!platform_defaults.empty()) {
            trace(QStringLiteral("step: platform defaults"));
            std::istringstream stream(platform_defaults);
            config::opt->ConfigNext(stream);
        }

        trace(QStringLiteral("step: user config"));
        try {
            config::opt->ConfigUser();
        } catch (agi::Exception const &e) {
            // A broken user config must not prevent the UI from starting.
            LOG_E("qt/config") << "Failed to load user config: " << e.GetMessage();
        }

        trace(QStringLiteral("step: done, config file = ") + OptionsFilePath());
        LOG_I("qt/config") << "Options loaded from " << OptionsFilePath().toStdString();
        return true;
    } catch (std::exception const &e) {
        return fail(QStringLiteral("Unhandled exception: ") + QString::fromUtf8(e.what()));
    } catch (...) {
        return fail(QStringLiteral("Unhandled unknown exception"));
    }
}

bool FlushOptions(QString *error) {
    if (!config::opt)
        return false;
    try {
        config::opt->Flush();
        return true;
    } catch (std::exception const &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return false;
    }
}

QString OptionsFilePath() {
    if (!config::path)
        return QString();
    return toQString(config::path->Decode("?user/config.json"));
}

} // namespace aegisub_qt
