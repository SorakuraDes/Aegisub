// agi::signal's `slots` member must be parsed before Qt headers.
#include "../../options.h"
#include <libaegisub/exception.h>
#include <libaegisub/hotkey.h>
#include <libaegisub/log.h>
#include <libaegisub/path.h>

#include "hotkey_vm.h"
#include "../qt_path.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMap>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>

#include <exception>

#include <utility>

namespace {
const auto defaultContext = QStringLiteral("Default");
const auto alwaysContext = QStringLiteral("Always");
const auto editContext = QStringLiteral("Subtitle Edit Box");
constexpr auto medusaOption = "Audio/Medusa Timing Hotkeys";

struct KeyName { int code; const char *name; const char *keypad = nullptr; };
constexpr KeyName keyNames[] = {
    {Qt::Key_Backspace, "Backspace"}, {Qt::Key_Tab, "Tab", "KP_Tab"},
    {Qt::Key_Return, "Enter", "KP_Enter"}, {Qt::Key_Enter, "KP_Enter"},
    {Qt::Key_Escape, "Escape"}, {Qt::Key_Space, "Space", "KP_Space"},
    {Qt::Key_Delete, "Delete", "KP_Delete"}, {Qt::Key_Insert, "Insert", "KP_insert"},
    {Qt::Key_Pause, "Pause"}, {Qt::Key_Print, "Print"}, {Qt::Key_Clear, nullptr, "KP_Begin"},
    {Qt::Key_Home, "Home", "KP_Home"}, {Qt::Key_End, "End", "KP_End"},
    {Qt::Key_Left, "Left", "KP_Left"}, {Qt::Key_Right, "Right", "KP_Right"},
    {Qt::Key_Up, "Up", "KP_Up"}, {Qt::Key_Down, "Down", "KP_Down"},
    {Qt::Key_PageUp, "PageUp", "KP_PageUp"}, {Qt::Key_PageDown, "PageDown", "KP_PageDown"},
    {Qt::Key_NumLock, "Num_Lock"}, {Qt::Key_ScrollLock, "Scroll_Lock"},
    {Qt::Key_Plus, "+", "KP_Add"}, {Qt::Key_Minus, "-", "KP_Subtract"},
    {Qt::Key_Asterisk, "*", "KP_Multiply"}, {Qt::Key_Slash, "/", "KP_Divide"},
    {Qt::Key_Period, ".", "KP_Decimal"}, {Qt::Key_Comma, ",", "KP_Decimal"},
    {Qt::Key_Equal, "=", "KP_Equal"},
};

QString eventKey(const QKeyEvent &event) {
    const auto mods = event.modifiers();
    constexpr auto allowed = Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::KeypadModifier;
    if (mods & ~allowed) return {}; // Meta, AltGr and platform-specific modifiers aren't wx combos.
    const bool keypad = mods.testFlag(Qt::KeypadModifier);
    const auto code = event.key() == Qt::Key_Backtab ? Qt::Key_Tab : event.key();
    QString base;
    for (const auto &entry : keyNames) {
        if (entry.code != code) continue;
        const auto *name = keypad && entry.keypad ? entry.keypad : entry.name;
        if (name) base = QString::fromLatin1(name);
        break;
    }
    if (base.isEmpty()) {
        if (code >= Qt::Key_F1 && code <= Qt::Key_F24) {
            base = (keypad && code <= Qt::Key_F4 ? QStringLiteral("KP_F") : QStringLiteral("F"))
                + QString::number(code - Qt::Key_F1 + 1);
        } else if (code > 32 && code < 127) {
            if (keypad && code >= Qt::Key_0 && code <= Qt::Key_9) base = QStringLiteral("KP_");
            base += QChar::fromLatin1(static_cast<char>(code));
        }
    }
    if (base.isEmpty()) return {};
    QString result;
    if (mods.testFlag(Qt::ControlModifier)) result += QLatin1String("Ctrl-");
    if (mods.testFlag(Qt::AltModifier)) result += QLatin1String("Alt-");
    if (mods.testFlag(Qt::ShiftModifier) || event.key() == Qt::Key_Backtab) result += QLatin1String("Shift-");
    return result + base;
}

QStringView baseKey(QStringView key) {
    for (const auto modifier : {u"Ctrl-", u"Alt-", u"Shift-"}) {
        const QStringView prefix(modifier);
        if (key.startsWith(prefix)) key = key.mid(prefix.size());
    }
    return key;
}

bool supportedKey(const QString &key) {
    const auto base = baseKey(key);
    if (base.size() == 1 && base[0].unicode() > 32 && base[0].unicode() < 127)
        return !base[0].isLower();
    for (const auto &entry : keyNames) {
        if ((entry.name && base == QLatin1String(entry.name)) ||
            (entry.keypad && base == QLatin1String(entry.keypad))) return true;
    }
    for (int i = 1; i <= 24; ++i)
        if (base == QStringLiteral("F") + QString::number(i)) return true;
    for (int i = 0; i <= 9; ++i) {
        if (base == QStringLiteral("KP_") + QString::number(i)) return true;
        if (i >= 1 && i <= 4 && base == QStringLiteral("KP_F") + QString::number(i)) return true;
    }
    return false;
}

QString displayKey(QString key) {
    const auto prefixSize = key.size() - baseKey(key).size();
    return key.left(prefixSize).replace(u'-', u'+') + key.mid(prefixSize);
}

QKeyCombination qtCombination(const QString &key) {
    auto base = baseKey(key);
    const auto modifiers = QStringView(key).left(key.size() - base.size());
    Qt::KeyboardModifiers mods;
    if (modifiers.contains(u"Ctrl-")) mods |= Qt::ControlModifier;
    if (modifiers.contains(u"Alt-")) mods |= Qt::AltModifier;
    if (modifiers.contains(u"Shift-")) mods |= Qt::ShiftModifier;
    if (base.startsWith(u"KP_")) mods |= Qt::KeypadModifier;
    for (const auto &entry : keyNames) {
        if ((entry.name && base == QLatin1String(entry.name)) ||
            (entry.keypad && base == QLatin1String(entry.keypad)))
            return QKeyCombination{mods, static_cast<Qt::Key>(entry.code)};
    }
    if (mods.testFlag(Qt::KeypadModifier)) base = base.mid(3);
    if (base.size() == 1) return QKeyCombination{mods, static_cast<Qt::Key>(base[0].unicode())};
    if (base.startsWith(u'F')) return QKeyCombination{mods, static_cast<Qt::Key>(Qt::Key_F1 + base.mid(1).toInt() - 1)};
    return QKeyCombination{mods, Qt::Key_unknown};
}

// The old format used objects with a key and a modifiers array. Keep those in
// the document as read, but pass the same string wx would construct to Scan.
QString bindingKey(const QJsonValue &value) {
    if (value.isString()) return value.toString();
    const auto object = value.toObject();
    const auto key = object.value(QStringLiteral("key"));
    const auto modifiers = object.value(QStringLiteral("modifiers"));
    if (!key.isString() || !modifiers.isArray()) return {};
    QString result;
    for (const auto &mod : modifiers.toArray()) {
        if (!mod.isString()) return {};
        result += mod.toString() + u'-';
    }
    return result + key.toString();
}

QStringList bindings(const QJsonValue &value) {
    QStringList result;
    for (const auto &entry : value.toArray()) {
        const auto key = bindingKey(entry);
        if (!key.isEmpty()) result << key;
    }
    return result;
}

std::unique_ptr<agi::hotkey::Hotkey> makeScanner(const QJsonObject &map) {
    if (!agi::log::log) return {}; // libaegisub's Hotkey requires initialized logging.
    QJsonObject projection;
    for (auto context = map.constBegin(); context != map.constEnd(); ++context) {
        const auto commands = context.value().toObject();
        QJsonObject keys;
        for (auto command = commands.constBegin(); command != commands.constEnd(); ++command)
            keys.insert(command.key(), QJsonArray::fromStringList(bindings(command.value())));
        projection.insert(context.key(), keys);
    }
    const auto json = QJsonDocument(projection).toJson(QJsonDocument::Compact);
    // Empty backing path: this is only a scanner. SetHotkeyMap first swaps
    // memory, then calls potentially throwing Flush(), which drops empty arrays.
    return std::make_unique<agi::hotkey::Hotkey>(agi::fs::path{},
        std::make_pair(json.constData(), static_cast<size_t>(json.size())));
}

struct Focus {
    QString context = defaultContext;
    bool text = false;
    bool body = false;
    bool composing = false;
};

QObject *focusParent(QObject *object) {
    if (auto *item = qobject_cast<QQuickItem *>(object))
        if (item->parentItem()) return item->parentItem();
    return object->parent();
}

Focus focusInfo(QObject *object) {
    Focus info;
    bool foundContext = false;
    for (; object; object = focusParent(object)) {
        if (!foundContext) {
            const auto context = object->property("hotkeyContext").toString();
            if (!context.isEmpty()) {
                info.context = context;
                foundContext = true;
            }
        }
        if (!info.text && (object->inherits("QQuickTextInput") || object->inherits("QQuickTextEdit"))) {
            info.text = true;
            info.body = object->objectName() == QLatin1String("editBoxTextArea");
            info.composing = object->property("inputMethodComposing").toBool();
            if (info.body && !foundContext) {
                info.context = editContext;
                foundContext = true;
            }
        }
    }
    return info;
}

bool textKeeps(const QKeyEvent &event, bool body, const QString &command) {
    const bool undoKey = event.matches(QKeySequence::Undo) || event.matches(QKeySequence::Redo);
    const auto mods = event.modifiers();
    const bool ctrlShiftZ = event.key() == Qt::Key_Z && mods == (Qt::ControlModifier | Qt::ShiftModifier);
    if (body && (undoKey || ctrlShiftZ) &&
        (command == QLatin1String("edit/undo") || command == QLatin1String("edit/redo")))
        return false; // Keep the document undo stack for body commits.
    if (mods.testFlag(Qt::GroupSwitchModifier)) return true;
    const bool commandModifier = mods.testFlag(Qt::ControlModifier) || mods.testFlag(Qt::MetaModifier);
    if (!commandModifier && !mods.testFlag(Qt::AltModifier)) {
        for (const auto ch : event.text())
            if (ch.isPrint()) return true;
        if (event.key() >= Qt::Key_Space && event.key() < Qt::Key_Escape) return true;
    }
    switch (event.key()) {
    case Qt::Key_Return: case Qt::Key_Enter:
        if (!body) return true;
        break;
    case Qt::Key_Backspace: case Qt::Key_Delete:
    case Qt::Key_Home: case Qt::Key_End: case Qt::Key_Left: case Qt::Key_Right:
    case Qt::Key_Up: case Qt::Key_Down: case Qt::Key_PageUp: case Qt::Key_PageDown:
    case Qt::Key_Tab: case Qt::Key_Backtab:
        return true;
    case Qt::Key_C: case Qt::Key_X: case Qt::Key_V: case Qt::Key_A:
        if (commandModifier && !mods.testFlag(Qt::AltModifier)) return true;
        break;
    default: break;
    }
    return event.matches(QKeySequence::Cut) || event.matches(QKeySequence::Copy) ||
           event.matches(QKeySequence::Paste) || event.matches(QKeySequence::SelectAll) ||
           undoKey || ctrlShiftZ;
}

bool textBlocks(const Focus &focus, const QKeyEvent &event, const QString &id) {
    if (!focus.text) return false;
    // Even a custom clipboard or row-moving chord targets the grid, not text.
    return textKeeps(event, focus.body, id) || id == QLatin1String("edit/line/copy") ||
        id == QLatin1String("edit/line/cut") || id == QLatin1String("edit/line/paste") ||
        id == QLatin1String("edit/line/paste/over") || id == QLatin1String("edit/line/delete") ||
        id == QLatin1String("grid/move/up") || id == QLatin1String("grid/move/down");
}

// Accept opaque key spellings from other frontends/older releases, but not
// empty, whitespace-only or control-bearing strings. Only supportedKey() keys
// can be recorded or advertised by this frontend.
bool validToken(const QJsonValue &value) {
    if (!value.isString()) return false;
    const auto token = value.toString();
    if (token.trimmed().isEmpty()) return false;
    for (const auto ch : token) if (ch.category() == QChar::Other_Control) return false;
    return true;
}

bool validBinding(const QJsonValue &value) {
    if (value.isString()) return validToken(value);
    if (!value.isObject()) return false;
    const auto object = value.toObject();
    const auto modifiers = object.value(QStringLiteral("modifiers"));
    if (!validToken(object.value(QStringLiteral("key"))) || !modifiers.isArray()) return false;
    for (const auto &modifier : modifiers.toArray()) if (!validToken(modifier)) return false;
    return true;
}

bool readObject(QFile &file, QJsonObject &object, QString &error) {
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return false;
    }
    QJsonParseError parseError{};
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (file.error() != QFile::NoError || !doc.isObject()) {
        error = file.error() != QFile::NoError ? file.errorString() :
            (parseError.error != QJsonParseError::NoError ? parseError.errorString() :
             HotkeyViewModel::tr("Expected a JSON object."));
        return false;
    }
    const auto map = doc.object();
    for (auto context = map.constBegin(); context != map.constEnd(); ++context) {
        if (!context.value().isObject()) {
            error = HotkeyViewModel::tr("Expected a hotkey context object for %1.").arg(context.key());
            return false;
        }
        const auto commands = context.value().toObject();
        for (auto command = commands.constBegin(); command != commands.constEnd(); ++command) {
            if (!command.value().isArray()) {
                error = HotkeyViewModel::tr("Expected a hotkey array for %1 / %2.").arg(context.key(), command.key());
                return false;
            }
            for (const auto &entry : command.value().toArray()) {
                if (validBinding(entry)) continue;
                error = HotkeyViewModel::tr("Invalid hotkey in %1 / %2.").arg(context.key(), command.key());
                return false;
            }
        }
    }
    object = map;
    return true;
}

// Compare normalized bindings; the core intentionally drops empty rows and
// contexts, sorts arrays, and writes legacy bindings as plain strings.
using BindingTable = QMap<QString, QMap<QString, QStringList>>;
BindingTable normalizedBindings(const QJsonObject &map) {
    BindingTable result;
    for (auto context = map.constBegin(); context != map.constEnd(); ++context) {
        const auto commands = context.value().toObject();
        for (auto command = commands.constBegin(); command != commands.constEnd(); ++command) {
            auto keys = bindings(command.value());
            if (keys.isEmpty()) continue;
            keys.sort();
            result[context.key()].insert(command.key(), std::move(keys));
        }
    }
    return result;
}

bool stageBindings(const QString &parent, const QJsonObject &map, const agi::hotkey::Hotkey &scanner, QString &error) {
    QTemporaryDir dir(QDir(parent).filePath(QStringLiteral(".hotkeys-XXXXXX")));
    if (!dir.isValid()) {
        error = dir.errorString();
        return false;
    }
    const auto path = dir.filePath(QStringLiteral("hotkey.json"));
    try {
        agi::hotkey::Hotkey scratch(aegisub_qt::ToNativePath(path), "{}");
        scratch.SetHotkeyMap(scanner.GetHotkeyMap());
    } catch (const agi::Exception &e) {
        error = QString::fromStdString(e.GetMessage());
        return false;
    } catch (const std::exception &e) {
        error = QString::fromUtf8(e.what());
        return false;
    }
    QFile file(path);
    QJsonObject staged;
    if (!readObject(file, staged, error)) return false;
    if (normalizedBindings(staged) == normalizedBindings(map)) return true;
    error = HotkeyViewModel::tr("The staged hotkey bindings do not match the requested changes.");
    return false;
}
} // namespace

struct HotkeyViewModel::State {
    struct Command { std::function<void()> action; std::function<bool()> enabled; };
    QMap<QString, QMap<QString, Command>> commands;
    QJsonObject defaults;
    QJsonObject map;
    std::unique_ptr<agi::hotkey::Hotkey> scanner;
    mutable QMap<QString, QMap<QString, QString>> menuWinners;
    QString path;
    QString error;
    QPointer<QQuickWindow> main;
    QMetaObject::Connection mainFocusChanged;
    QMetaObject::Connection mainDestroyed;
    QPointer<QQuickWindow> recorder;
    QPointer<QObject> preeditFocus;
    QMetaObject::Connection recorderDestroyed;
    agi::signal::Connection medusaChanged;
    std::function<void()> softLineBreak;
    bool defaultsValid = false;
    bool recording = false;
    bool always = false;
    int revision = 0;

    const Command *exact(const QString &id, const QString &context) const {
        auto command = commands.constFind(id);
        if (command == commands.cend()) return nullptr;
        auto callback = command->constFind(context);
        return callback != command->cend() ? &callback.value() : nullptr;
    }

    bool manages(const QString &id, const QString &context) const {
        return exact(id, context) || (commands.contains(id) &&
            (context == defaultContext || context == alwaysContext || context == editContext ||
             context == QLatin1String("Subtitle Grid") || context == QLatin1String("Video") ||
             context == QLatin1String("Audio")));
    }

    const Command *handler(const QString &id, const QString &context) const {
        if (auto *match = exact(id, context)) return match;
        if (auto *global = exact(id, defaultContext)) return global;
        const auto command = commands.constFind(id);
        return command == commands.cend() ? nullptr : &command->constBegin().value();
    }

    // Scan chooses the ID. Prefer the registration in the winning binding's
    // context if the same ID has a different handler in more than one context.
    const Command *bindingHandler(const QString &id, const QString &context, const QString &key) const {
        const auto order = {always ? alwaysContext : QString{}, context, defaultContext};
        for (const auto &source : order) {
            if (source.isEmpty() || !bindings(map.value(source).toObject().value(id)).contains(key)) continue;
            if (auto *callback = exact(id, source)) return callback;
            break;
        }
        return handler(id, context);
    }

    QString scan(const QString &context, const QString &key) const {
        return scanner ? QString::fromStdString(scanner->Scan(context.toStdString(), key.toStdString(), always)) : QString{};
    }

    QString menuWinner(const QString &context, const QString &key) const {
        auto &keys = menuWinners[context];
        const auto found = keys.constFind(key);
        if (found != keys.cend()) return found.value();
        return keys.insert(key, scan(context, key)).value();
    }
};

HotkeyViewModel::HotkeyViewModel(QObject *parent) : QObject(parent), state_(std::make_unique<State>()) {
    if (config::path) {
        const auto path = config::path->Decode("?user/hotkey.json");
#ifdef _WIN32
        state_->path = QString::fromStdWString(path.wstring());
#else
        state_->path = QString::fromStdString(path.string());
#endif
    }
    QFile defaults(QStringLiteral(":/default_hotkey.json"));
    QString detail;
    state_->defaultsValid = readObject(defaults, state_->defaults, detail);
    if (!state_->defaultsValid) fail(tr("Could not load default hotkeys: %1").arg(detail));
    state_->map = state_->defaults;
    QFile user(state_->path);
    if (!state_->path.isEmpty() && (user.exists() || QFileInfo(state_->path).isSymLink())) {
        if (!readObject(user, state_->map, detail)) fail(tr("Could not load user hotkeys: %1").arg(detail));
    }
    state_->scanner = makeScanner(state_->map);
    if (config::opt) {
        auto *option = OPT_SET(medusaOption);
        state_->always = option->GetBool();
        state_->medusaChanged = option->Subscribe([this, option] {
            state_->always = option->GetBool();
            state_->menuWinners.clear();
            touch();
        });
    }
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        app->installEventFilter(this);
        connect(app, &QGuiApplication::focusObjectChanged, this, [this] { state_->preeditFocus.clear(); });
    }
}

HotkeyViewModel::~HotkeyViewModel() {
    if (auto *app = QCoreApplication::instance()) app->removeEventFilter(this);
}

void HotkeyViewModel::attachMainWindow(QQuickWindow *window) {
    if (state_->main == window) return;
    disconnect(state_->mainFocusChanged);
    disconnect(state_->mainDestroyed);
    state_->main = window;
    if (window) {
        state_->mainFocusChanged = connect(window, &QQuickWindow::activeFocusItemChanged, this, &HotkeyViewModel::touch);
        state_->mainDestroyed = connect(window, &QObject::destroyed, this, &HotkeyViewModel::touch);
    }
    touch();
}
void HotkeyViewModel::setSoftLineBreakHandler(std::function<void()> handler) { state_->softLineBreak = std::move(handler); }

void HotkeyViewModel::registerCommand(const QString &id, const QString &context, std::function<void()> action,
                                      std::function<bool()> enabled) {
    if (id.isEmpty() || context.isEmpty() || !action) return;
    state_->commands[id].insert(context, {std::move(action), std::move(enabled)});
    touch();
}

int HotkeyViewModel::revision() const { return state_->revision; }
QString HotkeyViewModel::error() const { return state_->error; }
bool HotkeyViewModel::recording() const { return state_->recording; }

void HotkeyViewModel::touch() {
    ++state_->revision;
    emit changed();
}

bool HotkeyViewModel::fail(const QString &error) {
    if (state_->error != error) {
        state_->error = error;
        emit errorChanged();
    }
    return false;
}

QStringList HotkeyViewModel::contexts() const {
    QSet<QString> all;
    for (const auto &context : state_->map.keys()) all.insert(context);
    for (const auto &command : std::as_const(state_->commands))
        for (const auto &context : command.keys()) all.insert(context);
    QStringList sorted = all.values();
    sorted.sort(Qt::CaseInsensitive);
    return sorted;
}

QVariantList HotkeyViewModel::rows(const QString &context, const QString &search) const {
    QVariantList result;
    for (const auto &name : contexts()) {
        if (!context.isEmpty() && name != context) continue;
        const auto entries = state_->map.value(name).toObject();
        QMap<QString, QStringList> byCommand;
        QMap<QString, int> uses;
        for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
            const auto keys = bindings(it.value());
            byCommand.insert(it.key(), keys);
            QSet<QString> distinct(keys.cbegin(), keys.cend());
            for (const auto &key : distinct) ++uses[key];
        }
        for (auto it = state_->commands.cbegin(); it != state_->commands.cend(); ++it)
            if (it->contains(name) && !byCommand.contains(it.key())) byCommand.insert(it.key(), {});
        for (auto it = byCommand.cbegin(); it != byCommand.cend(); ++it) {
            bool matches = name.contains(search, Qt::CaseInsensitive) || it.key().contains(search, Qt::CaseInsensitive);
            bool conflict = false;
            for (const auto &key : it.value()) {
                matches |= key.contains(search, Qt::CaseInsensitive) || displayKey(key).contains(search, Qt::CaseInsensitive);
                conflict |= uses.value(key) > 1;
            }
            if (matches) result.append(QVariantMap{{QStringLiteral("context"), name},
                {QStringLiteral("command"), it.key()}, {QStringLiteral("bindings"), it.value()},
                {QStringLiteral("managed"), state_->manages(it.key(), name)},
                {QStringLiteral("conflict"), conflict}});
        }
    }
    return result;
}

bool HotkeyViewModel::save(const QJsonObject &map) {
    if (state_->path.isEmpty()) return fail(tr("No user hotkey configuration path is available."));
    const QFileInfo target(state_->path);
    if (!QDir().mkpath(target.absolutePath())) return fail(tr("Could not create the hotkey configuration directory."));
    auto scanner = makeScanner(map);
    if (!scanner) return fail(tr("The hotkey engine is unavailable."));
    QString detail;
    if (!stageBindings(target.absolutePath(), map, *scanner, detail))
        return fail(tr("Could not stage hotkeys: %1").arg(detail));
    // The verified core output is a lossy projection. Publish the equivalent
    // document that still retains untouched legacy values and empty collections.
    QSaveFile file(state_->path);
    file.setDirectWriteFallback(false);
    const auto data = QJsonDocument(map).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return fail(tr("Could not save hotkeys: %1").arg(file.errorString()));
    state_->scanner = std::move(scanner);
    state_->menuWinners.clear();
    state_->map = map;
    fail({});
    touch();
    return true;
}

bool HotkeyViewModel::rebind(const QString &context, const QString &command, const QString &oldKey, const QString &newKey) {
    if (!state_->manages(command, context)) return fail(tr("This hotkey is not managed by the Qt frontend."));
    auto group = state_->map.value(context).toObject();
    auto keys = group.value(command).toArray();
    qsizetype oldIndex = -1;
    if (!oldKey.isEmpty()) {
        for (qsizetype i = 0; i < keys.size(); ++i)
            if (bindingKey(keys.at(i)) == oldKey) { oldIndex = i; break; }
        if (oldIndex < 0) return fail(tr("The original hotkey no longer exists."));
    }
    if (oldKey == newKey) { fail({}); return true; }
    if (!newKey.isEmpty()) {
        if (!supportedKey(newKey)) return fail(tr("This key combination is not supported."));
        for (auto it = group.constBegin(); it != group.constEnd(); ++it) {
            if (!bindings(it.value()).contains(newKey)) continue;
            if (it.key() != command) return fail(tr("%1 is already assigned to %2.").arg(displayKey(newKey), it.key()));
            return fail(tr("%1 is already assigned to this command.").arg(displayKey(newKey)));
        }
    }
    if (oldIndex < 0) keys.append(newKey);
    else if (newKey.isEmpty()) keys.removeAt(oldIndex);
    else keys.replace(oldIndex, newKey);
    group.insert(command, keys);
    auto map = state_->map;
    map.insert(context, group);
    return save(map);
}

bool HotkeyViewModel::restoreDefaults() {
    if (!state_->defaultsValid) return fail(tr("The default hotkey configuration is unavailable."));
    auto map = state_->map;
    QSet<QString> contexts;
    for (const auto &name : this->contexts()) contexts.insert(name);
    for (const auto &name : state_->defaults.keys()) contexts.insert(name);
    for (const auto &name : contexts) {
        auto group = map.value(name).toObject();
        const auto defaults = state_->defaults.value(name).toObject();
        for (auto command = state_->commands.cbegin(); command != state_->commands.cend(); ++command) {
            if (!state_->manages(command.key(), name)) continue;
            const auto binding = defaults.value(command.key());
            if (binding.isUndefined()) group.remove(command.key());
            else group.insert(command.key(), binding);
        }
        if (!group.isEmpty() || map.contains(name)) map.insert(name, group);
    }
    return save(map);
}

QString HotkeyViewModel::menuShortcut(const QString &commandId, const QString &context) const {
    const auto focus = focusInfo(state_->main ? state_->main->activeFocusItem() : nullptr);
    const auto requested = state_->main || context.isEmpty() ? focus.context : context;
    if (!state_->handler(commandId, requested)) return {};
    const auto order = {state_->always ? alwaysContext : QString{}, requested, defaultContext};
    QSet<QString> checked;
    for (const auto &name : order) {
        if (name.isEmpty() || checked.contains(name) || (name == alwaysContext && !state_->always)) continue;
        checked.insert(name);
        const auto keys = bindings(state_->map.value(name).toObject().value(commandId));
        for (const auto &key : keys) {
            if (!supportedKey(key) || state_->menuWinner(requested, key) != commandId) continue;
            const auto chord = qtCombination(key);
            const QKeyEvent event(QEvent::KeyPress, chord.key(), chord.keyboardModifiers());
            if (!textBlocks(focus, event, commandId)) return displayKey(key);
        }
    }
    return {};
}

bool HotkeyViewModel::dispatch(const QString &id) {
    const auto focus = focusInfo(state_->main ? state_->main->activeFocusItem() : nullptr);
    const auto *handler = state_->handler(id, focus.context);
    if (!handler) return false;
    const auto command = *handler;
    if (command.enabled && !command.enabled()) return false;
    command.action();
    return true;
}

bool HotkeyViewModel::beginRecording(QObject *window) {
    auto *quickWindow = qobject_cast<QQuickWindow *>(window);
    if (!quickWindow || !quickWindow->isVisible()) return fail(tr("The hotkey recording window is unavailable."));
    if (state_->recording) return state_->recorder == quickWindow;
    state_->recorder = quickWindow;
    state_->recording = true;
    state_->recorderDestroyed = connect(quickWindow, &QObject::destroyed, this, &HotkeyViewModel::cancelRecording);
    fail({});
    emit recordingChanged();
    return true;
}

void HotkeyViewModel::cancelRecording() { finishRecording({}); }

void HotkeyViewModel::finishRecording(const QString &key) {
    if (!state_->recording) return;
    disconnect(state_->recorderDestroyed);
    state_->recorder.clear();
    state_->recording = false;
    emit recordingChanged();
    emit recorded(key);
}

bool HotkeyViewModel::eventFilter(QObject *watched, QEvent *event) {
    const auto type = event->type();
    if (state_->recording && watched == state_->recorder && (type == QEvent::Hide || type == QEvent::Close))
        cancelRecording();

    if (type != QEvent::InputMethod && type != QEvent::ShortcutOverride && type != QEvent::KeyPress) return false;
    auto *window = qobject_cast<QQuickWindow *>(watched);
    if (!window) {
        if (auto *item = qobject_cast<QQuickItem *>(watched)) window = item->window();
    }
    if (!window || window != QGuiApplication::focusWindow()) return false;
    if (state_->recording && window == state_->recorder && (type == QEvent::ShortcutOverride || type == QEvent::KeyPress)) {
        const auto &key = *static_cast<QKeyEvent *>(event);
        event->accept();
        if (type == QEvent::KeyPress && !key.isAutoRepeat()) {
            if (key.key() == Qt::Key_Escape && key.modifiers() == Qt::NoModifier) cancelRecording();
            else if (const auto combination = eventKey(key); !combination.isEmpty()) finishRecording(combination);
        }
        return true;
    }
    if (window != state_->main) return false;
    auto *focus = window->activeFocusItem();
    if (type == QEvent::InputMethod) {
        if (focus) {
            const auto &input = *static_cast<QInputMethodEvent *>(event);
            state_->preeditFocus = input.preeditString().isEmpty() ? nullptr : focus;
        }
        return false;
    }
    if (window->property("hotkeysBlocked").toBool() || state_->recording) return false;
    const auto info = focusInfo(focus);
    if (info.composing || (focus && state_->preeditFocus == focus)) return false;
    const auto &key = *static_cast<QKeyEvent *>(event);
    const auto combination = eventKey(key);
    if (combination.isEmpty()) return false;
    const auto id = state_->scan(info.context, combination);
    if (textBlocks(info, key, id)) return false;
    std::function<void()> action;
    if (const auto *handler = state_->bindingHandler(id, info.context, combination)) {
        const auto command = *handler;
        if (!command.enabled || command.enabled()) action = command.action;
    }
    if (!action && info.body && (combination == QLatin1String("Shift-Enter") || combination == QLatin1String("Shift-KP_Enter")))
        action = state_->softLineBreak;
    if (!action) return false;
    event->accept();
    if (type == QEvent::KeyPress && !key.isAutoRepeat()) action();
    return true;
}
