// Must come before any Qt header: agi::signal has members named `slots`, and Qt
// defines `signals`/`slots` as macros.
// src/options.h holds the config::{opt,path,...} globals and the OPT_GET/OPT_SET
// macros; it is the same header preferences.cpp uses in the wxWidgets frontend.
#include "../options.h"

#include "aegisub_core.h"
#include "qt_path.h"

#include <libaegisub/dispatch.h>
#include <libaegisub/exception.h>
#include <libaegisub/fs.h>
#include <libaegisub/log.h>
#include <libaegisub/make_unique.h>
#include <libaegisub/option.h>
#include <libaegisub/path.h>

#ifdef _WIN32
#include <boost/locale/generator.hpp>
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include <cstdio>
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

/// Creates \p path if needed and reports whether it is usable as a directory.
///
/// libaegisub creates ?local itself with agi::fs::CreateDirectory, which maps
/// Win32 errors through POSIX errc constants and logs localized (non-UTF-8)
/// error text before throwing; preparing the directories with Qt keeps that code
/// out of the normal startup path (see docs/qt6-frontend.md pitfalls 56/58).
bool prepareDirectory(QString const &path) {
    if (path.isEmpty())
        return false;

    const QFileInfo info(path);
    if (info.exists()) {
        // A regular file in the way (e.g. a read-only placeholder) means the
        // directory cannot be used.
        if (!info.isDir())
            return false;
    } else if (!QDir().mkpath(path)) {
        return false;
    }

    // QFileInfo::isWritable() is not reliable for directories on Windows
    // (MSVC's _waccess only checks that the directory exists), so prove
    // writability by creating and removing a real temporary file.
    QTemporaryFile probe(QDir(path).filePath(QStringLiteral(".aegisub_qt_write_XXXXXX")));
    probe.setAutoRemove(true);
    return probe.open();
}

/// Log emitter for the Qt frontend.
///
/// libaegisub's Windows EmitSTDOUT (libaegisub/windows/log_win.cpp) converts
/// every formatted message from UTF-8 to UTF-16 with charset::ConvertW before
/// calling OutputDebugStringW. Messages are not guaranteed to be UTF-8: e.g.
/// CHECKED_CALL in libaegisub/common/fs.cpp logs ec.message(), which on a
/// zh-CN machine is the ANSI (GBK) text "存取被拒。", and that makes the
/// conversion throw agi::charset::BadInput. The throw happens on the log
/// dispatch thread, where it cannot be delivered to the GUI thread and used to
/// terminate the process (pitfalls 56/57). Writing the raw bytes to stderr
/// needs no conversion and cannot throw. Only Exception/Assert/Warning are
/// emitted unless AEGISUB_QT_LOG_STDERR=1 asks for everything, so the normal
/// debug output stays out of automated stderr checks.
class StderrLogEmitter final : public agi::log::Emitter {
public:
    void log(agi::log::SinkMessage const &message) noexcept override {
        if (message.severity > agi::log::Warning && !verbose_)
            return;

        const char severity = agi::log::Severity_ID[int(message.severity)];
        std::fprintf(stderr, "[%s] %s:%d %c %s\n", message.section, message.file,
                     message.line, severity, message.message.c_str());
        std::fflush(stderr);
    }

private:
    // AEGISUB_QT_LOG_STDERR=1 (or any value not starting with '0') requests the
    // full log; the default only forwards Exception/Assert/Warning.
    bool verbose_ = [] {
        const QByteArray value = qgetenv("AEGISUB_QT_LOG_STDERR");
        return !value.isEmpty() && value.at(0) != '0';
    }();
};

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
#ifdef _WIN32
        // The shared providers pass path::string() to FFMS2, which expects UTF-8.
        // Match the wx filesystem locale without changing the process locale.
        agi::fs::path::imbue(boost::locale::generator().generate("en_US.UTF-8"));
#endif
        trace(QStringLiteral("step: dispatch/log"));
        // libaegisub's logging is used all over the core; without a sink every
        // LOG_* macro would dereference a null pointer.
        // libaegisub's dispatch queues hand C++ exceptions from background
        // thunks back through this hook (see Queue::Async in
        // libaegisub/common/dispatch.cpp: invoke_main(rethrow_exception)).
        // The wx frontend posts the thunk to its main loop and catches there,
        // but the Qt frontend has no wx main loop, so the thunk runs inline on
        // the dispatch worker and any escaping exception would call
        // std::terminate -> abort (Windows 0xC0000409, see pitfall 56).
        agi::dispatch::Init([](agi::dispatch::Thunk f) {
            try {
                f();
            } catch (agi::Exception const &e) {
                std::fprintf(stderr, "[dispatch] background exception: %s\n", e.GetMessage().c_str());
            } catch (std::exception const &e) {
                std::fprintf(stderr, "[dispatch] background exception: %s\n", e.what());
            } catch (...) {
                std::fputs("[dispatch] background unknown exception\n", stderr);
            }
            std::fflush(stderr);
        });
        if (!agi::log::log) {
            agi::log::log = new agi::log::LogSink;
            // Do not use EmitSTDOUT: it assumes every log message is UTF-8 and
            // throws agi::charset::BadInput on ANSI/GBK platform error text.
            agi::log::log->Subscribe(agi::make_unique<StderrLogEmitter>());
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
            const QString override_dir = qEnvironmentVariable("AEGISUB_QT_CONFIG_DIR");
            if (!override_dir.isEmpty()) {
                user_dir = override_dir;
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

            config::path->SetToken("?user", ToNativePath(user_dir));

            // Per-machine caches (ffms2 indexes, font caches, ...) live under
            // ?local, which defaults to %LOCALAPPDATA%\Aegisub. libaegisub's
            // create-directory helper logs a localized (non-UTF-8) error before
            // throwing when it cannot create it, which used to kill the process
            // on restricted tokens (pitfalls 56-58). Prepare it with Qt and fall
            // back to a writable directory next to the executable, mirroring the
            // .qt_user fallback above. Sandboxed runs must not write outside the
            // override directory.
            QString local_dir = override_dir.isEmpty()
                ? toQString(config::path->Decode("?local"))
                : user_dir;
            if (!prepareDirectory(local_dir)) {
                const QString unwritable_local = local_dir;
                if (override_dir.isEmpty()) {
                    // First fall back next to the executable (mirrors .qt_user);
                    // a read-only install directory is not usable either, so the
                    // last resort is the per-user temporary directory: cache
                    // data there is lost on reboot but media keeps working.
                    const QString fallbacks[] = {
                        app_dir.filePath(QStringLiteral(".qt_local")),
                        QDir(QDir::tempPath()).filePath(QStringLiteral("Aegisub-qt/local")),
                    };
                    for (const QString &candidate : fallbacks) {
                        if (!prepareDirectory(candidate))
                            continue;
                        trace(QStringLiteral("  ?local not writable (") + unwritable_local
                              + QStringLiteral("), falling back to ") + candidate);
                        local_dir = candidate;
                        break;
                    }
                }
                if (local_dir == unwritable_local) {
                    // Sandboxed runs must not write outside the override
                    // directory, and there is no fallback left.
                    trace(QStringLiteral("  ?local not writable: ") + local_dir);
                }
            }
            trace(QStringLiteral("  ?local = ") + local_dir);
            config::path->SetToken("?local", ToNativePath(local_dir));

            // ffms2 writes its index cache below ?local/ffms2cache; create it
            // here so the provider never has to (and never has a reason to log
            // a filesystem error about it).
            const QString ffms2_cache = local_dir + QStringLiteral("/ffms2cache");
            if (!prepareDirectory(ffms2_cache))
                trace(QStringLiteral("  ?local/ffms2cache not writable: ") + ffms2_cache);
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
    } catch (agi::Exception const &e) {
        // agi::Exception does NOT derive from std::exception (see
        // libaegisub/exception.h), so the generic catch below misses it. This
        // runs from aboutToQuit: an escaping exception would call
        // std::terminate -> abort (0xC0000409) when the user config cannot be
        // written, e.g. with a read-only profile (S14 verification, pitfall 61).
        if (error)
            *error = QString::fromStdString(e.GetMessage());
        return false;
    } catch (std::exception const &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return false;
    } catch (...) {
        if (error)
            *error = QStringLiteral("Unknown error while saving preferences");
        return false;
    }
}

QString OptionsFilePath() {
    if (!config::path)
        return QString();
    return toQString(config::path->Decode("?user/config.json"));
}

} // namespace aegisub_qt
