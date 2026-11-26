#include "lua_runtime.h"

#include "lua_ass_file.h"
#include "qt_path.h"
#include "version.h"

#include <libaegisub/color.h>
#include <libaegisub/lua/modules.h>
#include <libaegisub/lua/script_reader.h>
#include <libaegisub/lua/utils.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QScopeGuard>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace aegisub_qt::automation {
namespace {

QString stringAt(lua_State* L, int index)
{
    size_t size = 0;
    const char* value = lua_tolstring(L, index, &size);
    return value ? QString::fromUtf8(value, static_cast<qsizetype>(size)) : QString{};
}

QString checkedString(lua_State* L, int index)
{
    if (!lua_isstring(L, index))
        agi::lua::typerror(L, index, "string");
    return stringAt(L, index);
}

void pushString(lua_State* L, const QString& value)
{
    const auto utf8 = value.toUtf8();
    lua_pushlstring(L, utf8.constData(), static_cast<size_t>(utf8.size()));
}

QString errorAtTop(lua_State* L)
{
    const auto message = stringAt(L, -1);
    return message.isEmpty() ? QStringLiteral("Lua raised an error without a message.") : message;
}

void pushIndexes(lua_State* L, const QVector<int>& indexes)
{
    lua_createtable(L, static_cast<int>(indexes.size()), 0);
    for (int i = 0; i < indexes.size(); ++i) {
        lua_pushinteger(L, indexes[i]);
        lua_rawseti(L, -2, i + 1);
    }
}

std::optional<int> integerAt(lua_State* L, int index)
{
    if (lua_type(L, index) != LUA_TNUMBER)
        return std::nullopt;
    const auto num = lua_tonumber(L, index);
    if (!std::isfinite(num) || std::trunc(num) != num ||
        num < std::numeric_limits<int>::min() || num > std::numeric_limits<int>::max())
        return std::nullopt;
    return static_cast<int>(num);
}

void pushVariant(lua_State* L, const QVariant& value);

void pushMap(lua_State* L, const QVariantMap& map)
{
    lua_createtable(L, 0, static_cast<int>(map.size()));
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        pushString(L, it.key());
        pushVariant(L, it.value());
        lua_rawset(L, -3);
    }
}

void pushVariant(lua_State* L, const QVariant& value)
{
    switch (value.typeId()) {
        case QMetaType::UnknownType:
        case QMetaType::Nullptr: lua_pushnil(L); break;
        case QMetaType::Bool: lua_pushboolean(L, value.toBool()); break;
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
        case QMetaType::Float:
        case QMetaType::Double: lua_pushnumber(L, value.toDouble()); break;
        case QMetaType::QVariantMap: pushMap(L, value.toMap()); break;
        case QMetaType::QVariantList:
        case QMetaType::QStringList: {
            const auto list = value.toList();
            lua_createtable(L, static_cast<int>(list.size()), 0);
            for (int i = 0; i < list.size(); ++i) {
                pushVariant(L, list[i]);
                lua_rawseti(L, -2, i + 1);
            }
            break;
        }
        default: pushString(L, value.toString()); break;
    }
}

void pushOptional(lua_State* L, std::optional<int> value)
{
    if (value) lua_pushinteger(L, *value);
    else lua_pushnil(L);
}

template<lua_CFunction Callback>
int luaCallback(lua_State* L)
{
    try {
        return Callback(L);
    }
    catch (const agi::lua::error_tag&) {
        // The error object is already on the Lua stack.
    }
    catch (const agi::Exception& e) {
        agi::lua::push_value(L, e.GetMessage());
    }
    catch (const std::exception& e) {
        lua_pushstring(L, e.what());
    }
    // Complete C++ exception unwinding before LuaJIT raises the Lua error.
    return lua_error(L);
}

int functionRef(lua_State* L, int index)
{
    if (!lua_isfunction(L, index)) return LUA_NOREF;
    lua_pushvalue(L, index);
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

template<typename Read>
auto readField(lua_State* L, const char* key, Read read)
{
    lua_pushstring(L, key);
    lua_rawget(L, -2);
    auto value = read();
    lua_pop(L, 1);
    return value;
}

QString stringField(lua_State* L, const char* key, const QString& fallback = {})
{
    return readField(L, key, [&] { return lua_isstring(L, -1) ? stringAt(L, -1) : fallback; });
}

double numberField(lua_State* L, const char* key, double fallback)
{
    return readField(L, key, [&] {
        const auto value = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : fallback;
        return std::isfinite(value) ? value : fallback;
    });
}

int boundedInt(double value, int min, int max)
{
    return static_cast<int>(std::clamp(value, static_cast<double>(min), static_cast<double>(max)));
}

QString controlColor(const QString& kind, const QString& input)
{
    const auto text = input.trimmed();
    if (kind == QStringLiteral("alpha")) {
        static const QRegularExpression alpha(QStringLiteral("^(?:#([0-9a-f]{2})|&h([0-9a-f]{2})&?|([0-9a-f]{2}))$"),
                                              QRegularExpression::CaseInsensitiveOption);
        const auto match = alpha.match(text);
        if (!match.hasMatch()) return QStringLiteral("#00");
        return u'#' + (match.captured(1) + match.captured(2) + match.captured(3)).toUpper();
    }

    // The core parser handles ASS/SSA and CSS RGB. Explicitly decode RGBA
    // hex because the core RGB parser does not consume the last two digits.
    static const QRegularExpression rgba(QStringLiteral("^#[0-9a-fA-F]{8}$"));
    agi::Color color(text.toStdString());
    if (rgba.match(text).hasMatch()) {
        color = agi::Color(text.mid(0, 7).toStdString());
        color.a = static_cast<unsigned char>(text.mid(7).toUInt(nullptr, 16));
    }
    return QString::fromStdString(color.GetHexFormatted(kind == QStringLiteral("coloralpha")));
}

QVariant normalizedValue(const QVariantMap& control, const QVariant& input)
{
    const auto kind = control.value(QStringLiteral("class")).toString();
    const auto fallback = control.value(QStringLiteral("value"));
    if (kind == QStringLiteral("intedit") || kind == QStringLiteral("floatedit")) {
        if (input.typeId() == QMetaType::Bool) return fallback;
        bool ok = false;
        const double value = input.toDouble(&ok);
        if (!ok || !std::isfinite(value)) return fallback;
        if (kind == QStringLiteral("intedit"))
            return boundedInt(value, control.value(QStringLiteral("min")).toInt(), control.value(QStringLiteral("max")).toInt());
        return std::clamp(value, control.value(QStringLiteral("min")).toDouble(), control.value(QStringLiteral("max")).toDouble());
    }
    if (kind == QStringLiteral("checkbox")) return input.typeId() == QMetaType::Bool ? input : fallback;
    if (input.typeId() != QMetaType::QString) return fallback;
    if (kind == QStringLiteral("alpha") || kind == QStringLiteral("color") || kind == QStringLiteral("coloralpha"))
        return controlColor(kind, input.toString());
    return input;
}

QVariantList readControls(lua_State* L, int table, const QVariantMap& previous = {})
{
    if (!lua_istable(L, table)) throw std::runtime_error("Cannot create config dialog from something non-table");
    lua_pushvalue(L, table);
    QVariantList controls;
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        if (!lua_istable(L, -1)) throw std::runtime_error("Bad dialog control table entry");
        const auto kind = stringField(L, "class").toLower();
        const auto name = stringField(L, "name");
        // Limit both origins and ends to keep QML GridLayout tracks bounded.
        constexpr int gridLimit = 256;
        const int x = boundedInt(numberField(L, "x", 0), 0, gridLimit - 1);
        const int y = boundedInt(numberField(L, "y", 0), 0, gridLimit - 1);
        QVariantMap control{
            {QStringLiteral("class"), kind}, {QStringLiteral("name"), name},
            {QStringLiteral("hint"), stringField(L, "hint")},
            {QStringLiteral("x"), x}, {QStringLiteral("y"), y},
            {QStringLiteral("width"), boundedInt(numberField(L, "width", 1), 1, gridLimit - x)},
            {QStringLiteral("height"), boundedInt(numberField(L, "height", 1), 1, gridLimit - y)},
            {QStringLiteral("label"), stringField(L, "label")}
        };
        QVariant value;
        if (kind == QStringLiteral("label")) {
            // Labels never appear in the result table.
        }
        else if (kind == QStringLiteral("edit") || kind == QStringLiteral("textbox") || kind == QStringLiteral("alpha")) {
            const auto text = stringField(L, "text", stringField(L, "value"));
            value = kind == QStringLiteral("alpha") ? controlColor(kind, text) : text;
        }
        else if (kind == QStringLiteral("color") || kind == QStringLiteral("coloralpha"))
            value = controlColor(kind, stringField(L, "value"));
        else if (kind == QStringLiteral("dropdown")) {
            value = stringField(L, "value");
            control.insert(QStringLiteral("items"), readField(L, "items", [&] {
                QVariantList items;
                if (lua_istable(L, -1)) {
                    lua_pushnil(L);
                    while (lua_next(L, -2)) {
                        if (lua_isstring(L, -1)) items.append(stringAt(L, -1));
                        lua_pop(L, 1);
                    }
                }
                return items;
            }));
        }
        else if (kind == QStringLiteral("checkbox"))
            value = readField(L, "value", [&] { return lua_isboolean(L, -1) && lua_toboolean(L, -1); });
        else if (kind == QStringLiteral("intedit")) {
            constexpr int lowest = std::numeric_limits<int>::min();
            constexpr int highest = std::numeric_limits<int>::max();
            int min = boundedInt(numberField(L, "min", lowest), lowest, highest);
            int max = boundedInt(numberField(L, "max", highest), lowest, highest);
            if (min >= max) { min = lowest; max = highest; }
            control.insert(QStringLiteral("min"), min);
            control.insert(QStringLiteral("max"), max);
            control.insert(QStringLiteral("step"), 1);
            value = boundedInt(numberField(L, "value", 0), min, max);
        }
        else if (kind == QStringLiteral("floatedit")) {
            constexpr double extreme = std::numeric_limits<double>::max();
            double min = numberField(L, "min", -extreme);
            double max = numberField(L, "max", extreme);
            const double step = numberField(L, "step", 0);
            if (min >= max) { min = -extreme; max = extreme; }
            if (step != 0) {
                if (min == -extreme) min = 0;
                if (max == extreme) max = 100;
                if (min >= max) { min = 0; max = 100; }
            }
            control.insert(QStringLiteral("min"), min);
            control.insert(QStringLiteral("max"), max);
            control.insert(QStringLiteral("step"), step);
            value = std::clamp(numberField(L, "value", 0), min, max);
        }
        else throw std::runtime_error("Unknown dialog control class");

        if (value.isValid()) {
            control.insert(QStringLiteral("value"), value);
            if (const auto it = previous.constFind(name); it != previous.cend())
                control.insert(QStringLiteral("value"), normalizedValue(control, it.value()));
        }
        controls.append(control);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return controls;
}

QVariantMap dialogValues(const QVariantList& controls, const QVariantMap& response)
{
    QVariantMap values;
    for (const auto& entry : controls) {
        const auto control = entry.toMap();
        const auto name = control.value(QStringLiteral("name")).toString();
        if (!control.contains(QStringLiteral("value"))) {
            // A later label with a duplicate name overwrote the original value with nil.
            values.remove(name);
            continue;
        }
        values.insert(name, normalizedValue(control, response.value(name)));
    }
    return values;
}

QVariantMap dialogButton(const QString& text, const QString& role, const QString& result)
{
    return {{QStringLiteral("text"), text}, {QStringLiteral("role"), role}, {QStringLiteral("result"), result}};
}

QVariantList readButtons(lua_State* L)
{
    QVariantList buttons;
    if (lua_istable(L, 2)) {
        lua_pushnil(L);
        while (lua_next(L, 2)) {
            const auto text = checkedString(L, -1);
            buttons.append(dialogButton(text, QStringLiteral("custom"), text));
            lua_pop(L, 1);
        }
    }
    if (lua_istable(L, 3)) {
        static const QStringList roles{
            QStringLiteral("ok"), QStringLiteral("yes"), QStringLiteral("save"), QStringLiteral("apply"),
            QStringLiteral("close"), QStringLiteral("no"), QStringLiteral("cancel"), QStringLiteral("help"),
            QStringLiteral("context_help")
        };
        lua_pushnil(L);
        while (lua_next(L, 3)) {
            lua_pushvalue(L, -2); // Conversion must not change the key used by lua_next.
            const auto role = checkedString(L, -1);
            lua_pop(L, 1);
            const auto text = checkedString(L, -1);
            const auto button = std::find_if(buttons.begin(), buttons.end(), [&](const QVariant& entry) {
                return entry.toMap().value(QStringLiteral("text")).toString() == text;
            });
            if (button == buttons.end()) throw std::runtime_error("Invalid dialog button mapping");
            *button = dialogButton(text, roles.contains(role) ? role : QStringLiteral("custom"),
                                   role == QStringLiteral("cancel") ? QString{} : text);
            lua_pop(L, 1);
        }
    }
    if (buttons.isEmpty()) {
        buttons.append(dialogButton(QStringLiteral("OK"), QStringLiteral("ok"), QStringLiteral("")));
        buttons.append(dialogButton(QStringLiteral("Cancel"), QStringLiteral("cancel"), QStringLiteral("")));
    }
    return buttons;
}

} // namespace

struct LuaScript::State {
    struct MacroFunctions {
        int run;
        int validate;
        int isActive;
    };

    struct FilterFunctions {
        int run;
        int configure;
    };

    lua_State* L = nullptr;
    Host host;
    const Host* executionHost = nullptr;

    const Host& currentHost() const { return executionHost ? *executionHost : host; }
    ScriptMetadata metadata;
    QVector<Macro> macros;
    QVector<Filter> filters;
    QVector<MacroFunctions> macroFunctions;
    QVector<FilterFunctions> filterFunctions;
    QString filename;
    QStringList searchDirs;
    QString undoLabel;
    int disableJitRef = LUA_NOREF;
    int flushJitRef = LUA_NOREF;
    bool loading = false;
    bool invoking = false;
    bool runningMacro = false;
    bool cancelled = false;
    int cancelledHookTicks = 0;

    struct Invocation {
        State& state;
        const Host* previousHost;

        Invocation(State& state, const Host* host = nullptr, bool macro = false)
            : state(state), previousHost(std::exchange(state.executionHost, host))
        {
            state.invoking = true;
            state.runningMacro = macro;
            state.cancelled = false;
            state.cancelledHookTicks = 0;
        }

        ~Invocation()
        {
            state.executionHost = previousHost;
            state.invoking = false;
            state.runningMacro = false;
            state.undoLabel.clear();
        }
    };

    ~State()
    {
        if (L) lua_close(L);
    }

    static State& get(lua_State* L)
    {
        lua_pushlightuserdata(L, &registryKey);
        lua_rawget(L, LUA_REGISTRYINDEX);
        auto* state = static_cast<State*>(lua_touserdata(L, -1));
        lua_pop(L, 1);
        return *state;
    }

    static inline char registryKey;

    bool pollCancellation()
    {
        const auto& callback = currentHost().isCancelled;
        if (!cancelled && callback) cancelled = callback();
        return cancelled;
    }

    static void cancelHook(lua_State* L, lua_Debug*)
    {
        auto& state = get(L);
        // Lua errors are catchable; the C++ latch remains set independently.
        try {
            if (!state.pollCancellation()) return;
            // Give cooperative loops time to observe is_cancelled() first.
            if (state.cancelledHookTicks < 8) {
                ++state.cancelledHookTicks;
                return;
            }
        }
        catch (...) {
            state.cancelled = true;
        }
        lua_pushnil(L);
        lua_error(L);
    }

    static int cancel(lua_State* L)
    {
        get(L).cancelled = true;
        lua_pushnil(L);
        throw agi::lua::error_tag{};
    }

    static int isCancelled(lua_State* L)
    {
        lua_pushboolean(L, get(L).pollCancellation());
        return 1;
    }

    static int registerMacro(lua_State* L)
    {
        auto& self = get(L);
        if (!self.loading) return agi::lua::error(L, "Macros can only be registered while loading a script");
        const auto name = checkedString(L, 1);
        if (!lua_isfunction(L, 3)) return agi::lua::typerror(L, 3, "function");
        if (std::any_of(self.macros.cbegin(), self.macros.cend(), [&](const Macro& macro) { return macro.name == name; }))
            return agi::lua::error(L, "A macro with this name is already registered in the script");
        const MacroFunctions functions{ functionRef(L, 3), functionRef(L, 4), functionRef(L, 5) };
        self.macros.append(Macro{name, stringAt(L, 2), functions.validate != LUA_NOREF, functions.isActive != LUA_NOREF});
        self.macroFunctions.append(functions);
        return 0;
    }

    static int registerFilter(lua_State* L)
    {
        auto& self = get(L);
        if (!self.loading) return agi::lua::error(L, "Filters can only be registered while loading a script");
        const auto name = checkedString(L, 1);
        if (!lua_isfunction(L, 4)) return agi::lua::typerror(L, 4, "function");
        const FilterFunctions functions{functionRef(L, 4), functionRef(L, 5)};
        self.filters.append(Filter{name, stringAt(L, 2), integerAt(L, 3).value_or(0), functions.configure != LUA_NOREF});
        self.filterFunctions.append(functions);
        return 0;
    }

    static int gettext(lua_State* L)
    {
        const auto input = checkedString(L, 1);
        const auto& translate = get(L).currentHost().translate;
        pushString(L, translate ? translate(input) : input);
        return 1;
    }

    static int log(lua_State* L)
    {
        const auto& callback = get(L).currentHost().log;
        if (!callback) return 0;
        int level = 0;
        if (lua_type(L, 1) == LUA_TNUMBER) {
            level = integerAt(L, 1).value_or(0);
            lua_remove(L, 1);
        }
        if (lua_gettop(L) > 1) {
            const int args = lua_gettop(L);
            lua_getglobal(L, "string");
            lua_getfield(L, -1, "format");
            lua_remove(L, -2);
            lua_insert(L, 1);
            if (lua_pcall(L, args, 1, 0) != 0) throw agi::lua::error_tag{};
        }
        callback(level, checkedString(L, 1));
        return 0;
    }

    template<auto Member>
    static int timeConversion(lua_State* L)
    {
        const auto& callback = get(L).currentHost().*Member;
        const auto value = integerAt(L, 1);
        pushOptional(L, callback && value ? callback(*value) : std::nullopt);
        return 1;
    }

    static int videoSize(lua_State* L)
    {
        const auto& callback = get(L).currentHost().videoSize;
        if (auto size = callback ? callback() : std::nullopt) {
            lua_pushinteger(L, size->width);
            lua_pushinteger(L, size->height);
            lua_pushnumber(L, size->aspectRatio);
            lua_pushinteger(L, size->aspectRatioType);
            return 4;
        }
        lua_pushnil(L);
        return 1;
    }

    static int keyframes(lua_State* L)
    {
        const auto& callback = get(L).currentHost().keyframes;
        if (auto indexes = callback ? callback() : std::nullopt) pushIndexes(L, *indexes);
        else lua_pushnil(L);
        return 1;
    }

    static int properties(lua_State* L)
    {
        const auto& callback = get(L).currentHost().properties;
        if (callback) pushMap(L, callback());
        else lua_pushnil(L);
        return 1;
    }

    static int decodePath(lua_State* L)
    {
        const auto input = checkedString(L, 1);
        const auto& callback = get(L).currentHost().decodePath;
        pushString(L, callback ? callback(input) : input);
        return 1;
    }

    static int fileName(lua_State* L)
    {
        const auto& callback = get(L).currentHost().fileName;
        const QString filename = callback ? callback() : QString{};
        if (filename.isEmpty()) lua_pushnil(L);
        else pushString(L, filename);
        return 1;
    }

    static int progressSet(lua_State* L)
    {
        if (const auto& callback = get(L).currentHost().progressSet) {
            const double percent = lua_tonumber(L, 1);
            if (std::isfinite(percent)) callback(std::clamp(percent, 0.0, 100.0));
        }
        return 0;
    }

    template<auto Member>
    static int progressText(lua_State* L)
    {
        if (const auto& callback = get(L).currentHost().*Member) callback(checkedString(L, 1));
        return 0;
    }

    static int setUndoPoint(lua_State* L)
    {
        auto& self = get(L);
        if (!self.runningMacro) return agi::lua::error(L, "Undo points are only available while running a macro");
        auto label = checkedString(L, 1);
        if (!label.isEmpty()) self.undoLabel = std::move(label);
        return 0;
    }

    static int displayDialog(lua_State* L)
    {
        const auto& self = get(L);
        const auto& host = self.currentHost();
        if (!self.runningMacro || !host.allowDialogs || !host.dialog) {
            lua_pushboolean(L, false);
            lua_newtable(L);
            return 2;
        }
        const auto controls = readControls(L, 1);
        const auto buttons = readButtons(L);
        const auto response = host.dialog({{QStringLiteral("controls"), controls}, {QStringLiteral("buttons"), buttons}});
        const auto button = response.value(QStringLiteral("button"));
        const auto result = button.toString();
        const bool accepted = button.typeId() == QMetaType::QString &&
            std::any_of(buttons.cbegin(), buttons.cend(), [&](const QVariant& item) {
                const auto definition = item.toMap();
                return definition.value(QStringLiteral("role")).toString() != QStringLiteral("cancel") &&
                       definition.value(QStringLiteral("result")).toString() == result;
            });
        if (accepted) pushString(L, result);
        else lua_pushboolean(L, false);
        pushMap(L, dialogValues(controls, response.value(QStringLiteral("values")).toMap()));
        return 2;
    }

    template<bool Save>
    static int chooseFile(lua_State* L)
    {
        const auto& self = get(L);
        const auto& host = self.currentHost();
        if (!self.runningMacro || !host.allowDialogs || !host.chooseFile) {
            lua_pushnil(L);
            return 1;
        }
        const bool multiple = !Save && lua_toboolean(L, 5);
        const bool mustExist = !Save && (lua_isnoneornil(L, 6) || lua_toboolean(L, 6));
        const bool dontPrompt = Save && lua_toboolean(L, 5);
        const auto paths = host.chooseFile({
            {QStringLiteral("title"), checkedString(L, 1)}, {QStringLiteral("directory"), checkedString(L, 2)},
            {QStringLiteral("file"), checkedString(L, 3)}, {QStringLiteral("wildcard"), checkedString(L, 4)},
            {QStringLiteral("save"), Save}, {QStringLiteral("multiple"), multiple},
            {QStringLiteral("mustExist"), mustExist}, {QStringLiteral("dontPrompt"), dontPrompt}
        });
        if (paths.isEmpty()) lua_pushnil(L);
        else if (multiple) {
            lua_createtable(L, static_cast<int>(paths.size()), 0);
            for (int i = 0; i < paths.size(); ++i) {
                pushString(L, paths[i]);
                lua_rawseti(L, -2, i + 1);
            }
        }
        else pushString(L, paths.first());
        return 1;
    }

    static int noValue(lua_State* L)
    {
        lua_pushnil(L);
        return 1;
    }

    static int textExtents(lua_State* L)
    {
        if (!lua_istable(L, 1)) return agi::lua::typerror(L, 1, "table");
        const auto fieldString = [L](const char* field) {
            lua_pushstring(L, field);
            lua_rawget(L, 1);
            auto value = stringAt(L, -1);
            lua_pop(L, 1);
            return value;
        };
        const auto fieldNumber = [L](const char* field, double fallback) {
            lua_pushstring(L, field);
            lua_rawget(L, 1);
            const auto value = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : fallback;
            lua_pop(L, 1);
            return std::isfinite(value) ? value : fallback;
        };
        const auto fieldBool = [L](const char* field) {
            lua_pushstring(L, field);
            lua_rawget(L, 1);
            const bool value = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
            return value;
        };
        if (fieldString("class").compare(QStringLiteral("style"), Qt::CaseInsensitive) != 0)
            return agi::lua::error(L, "Not a style entry");
        const auto text = checkedString(L, 2);
        if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
            return agi::lua::error(L, "Text metrics require a Qt GUI application");

        constexpr double oversample = 8.0;
        const auto family = fieldString("fontname");
        const auto size = qRound(std::clamp(fieldNumber("fontsize", 20), 1.0, 4096.0) * oversample);
        const auto bold = fieldBool("bold"), italic = fieldBool("italic");
        const auto spacing = fieldNumber("spacing", 0) * oversample;
        const double x = fieldNumber("scale_x", 100) / (100 * oversample);
        const double y = fieldNumber("scale_y", 100) / (100 * oversample);
        auto values = std::make_shared<QVector<double>>();
        auto measure = [=] {
            QFont font(family);
            font.setPixelSize(size);
            font.setBold(bold);
            font.setItalic(italic);
            font.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
            const QFontMetricsF metrics(font);
            *values = {metrics.horizontalAdvance(text) * x, metrics.height() * y,
                       metrics.descent() * y, metrics.leading() * y};
        };
        if (const auto &onGui = get(L).currentHost().onGui) onGui(measure);
        else measure();
        if (values->isEmpty()) return 0;
        for (double value : *values) lua_pushnumber(L, value);
        return 4;
    }

    static bool loadChunk(lua_State* L, const QString& path)
    {
        if (!path.endsWith(QStringLiteral(".moon"), Qt::CaseInsensitive)) {
            try {
                return agi::lua::LoadFile(L, ToNativePath(path));
            } catch (const agi::Exception& error) {
                pushString(L, QString::fromStdString(error.GetMessage()));
                return false;
            } catch (const std::exception& error) {
                pushString(L, QString::fromUtf8(error.what()));
                return false;
            }
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            pushString(L, QStringLiteral("Cannot read %1: %2").arg(path, file.errorString()));
            return false;
        }
        auto source = file.readAll();
        if (file.error() != QFileDevice::NoError) {
            pushString(L, QStringLiteral("Cannot read %1: %2").arg(path, file.errorString()));
            return false;
        }
        if (source.startsWith("\xEF\xBB\xBF")) source.remove(0, 3);
        const auto name = (QStringLiteral("@") + path).toUtf8();

        lua_getglobal(L, "require");
        lua_pushliteral(L, "moonscript");
        if (lua_pcall(L, 1, 1, 0) != 0) return false;
        lua_getfield(L, -1, "loadstring");
        lua_remove(L, -2);
        lua_pushlstring(L, source.constData(), static_cast<size_t>(source.size()));
        lua_pushvalue(L, -1);
        const auto registryName = QByteArrayLiteral("raw moonscript: ") + name;
        lua_setfield(L, LUA_REGISTRYINDEX, registryName.constData());
        lua_pushlstring(L, name.constData(), static_cast<size_t>(name.size()));
        if (lua_pcall(L, 2, 2, 0) != 0) return false;
        if (!lua_isfunction(L, -2)) {
            lua_remove(L, -2);
            return false;
        }
        lua_pop(L, 1);
        return true;
    }

    static int include(lua_State* L)
    {
        const auto& self = get(L);
        const auto requested = QDir::fromNativeSeparators(checkedString(L, 1));
        QString path;
        if (requested.contains(u'/') || QDir::isAbsolutePath(requested)) {
            const auto candidate = QFileInfo(self.filename).absoluteDir().absoluteFilePath(requested);
            if (QFileInfo(candidate).isFile()) path = candidate;
        }
        else {
            for (const auto& directory : self.searchDirs) {
                const auto candidate = QDir(directory).filePath(requested);
                if (QFileInfo(candidate).isFile()) {
                    path = candidate;
                    break;
                }
            }
        }
        if (path.isEmpty()) {
            pushString(L, QStringLiteral("Lua include not found: %1").arg(requested));
            throw agi::lua::error_tag{};
        }
        lua_settop(L, 0);
        if (!loadChunk(L, path) || lua_pcall(L, 0, LUA_MULTRET, 0) != 0) throw agi::lua::error_tag{};
        return lua_gettop(L);
    }

    static int moduleLoader(lua_State* L)
    {
        auto module = checkedString(L, 1);
        module.replace(u'.', u'/');
        lua_getglobal(L, "package");
        lua_getfield(L, -1, "path");
        const auto paths = stringAt(L, -1).split(u';', Qt::SkipEmptyParts);
        lua_pop(L, 2);
        for (auto path : paths) {
            path.replace(u'?', module);
            if (path.endsWith(QStringLiteral(".lua"), Qt::CaseInsensitive)) {
                const auto moon = path.chopped(4) + QStringLiteral(".moon");
                if (QFileInfo(moon).isFile()) path = moon;
            }
            if (!QFileInfo(path).isFile()) continue;
            if (!loadChunk(L, path)) throw agi::lua::error_tag{};
            return 1;
        }
        lua_pushfstring(L, "\n\tno Lua module '%s' in package.path", lua_tostring(L, 1));
        return 1;
    }

    template<lua_CFunction Callback>
    static void setFunction(lua_State* L, const char* name)
    {
        lua_pushcfunction(L, luaCallback<Callback>);
        lua_setfield(L, -2, name);
    }

    bool installApi()
    {
        agi::lua::preload_modules(L);
        lua_getglobal(L, "jit");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "off");
            disableJitRef = functionRef(L, -1);
            lua_pop(L, 1);
            lua_getfield(L, -1, "flush");
            flushJitRef = functionRef(L, -1);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        lua_pushlightuserdata(L, &registryKey);
        lua_pushlightuserdata(L, this);
        lua_rawset(L, LUA_REGISTRYINDEX);

        lua_pushnil(L);
        lua_setglobal(L, "dofile");
        lua_pushnil(L);
        lua_setglobal(L, "loadfile");
        lua_pushcfunction(L, luaCallback<include>);
        lua_setglobal(L, "include");

        lua_getglobal(L, "package");
        QStringList paths;
        for (const auto& dir : searchDirs) {
            paths.append(QDir(dir).filePath(QStringLiteral("?.lua")));
            paths.append(QDir(dir).filePath(QStringLiteral("?/init.lua")));
        }
#ifndef Q_OS_WIN
        lua_getfield(L, -1, "path");
        paths.append(stringAt(L, -1));
        lua_pop(L, 1);
#endif
        pushString(L, paths.join(u';'));
        lua_setfield(L, -2, "path");
        lua_getfield(L, -1, "loaders");
        lua_pushcfunction(L, luaCallback<moduleLoader>);
        lua_rawseti(L, -2, 2);
        lua_pop(L, 2);

#ifdef Q_OS_WIN
        const bool hasUnicodePatch = std::any_of(searchDirs.cbegin(), searchDirs.cend(), [](const QString& dir) {
            return QFileInfo(QDir(dir).filePath(QStringLiteral("unicode-monkeypatch.lua"))).isFile();
        });
        if (hasUnicodePatch) {
            lua_getglobal(L, "require");
            lua_pushliteral(L, "unicode-monkeypatch");
            if (lua_pcall(L, 1, 0, 0) != 0) return false;
        }
#endif

        lua_newtable(L);
        lua_pushinteger(L, 4);
        lua_setfield(L, -2, "lua_automation_version");
        lua_pushcfunction(L, [](lua_State* state) -> int {
            lua_pushliteral(state, LUA_VERSION);
            return 1;
        });
        lua_setfield(L, -2, "lua_version");
        lua_pushcfunction(L, [](lua_State* state) -> int {
            lua_pushstring(state, GetAegisubShortVersionString());
            return 1;
        });
        lua_setfield(L, -2, "version");
        setFunction<registerMacro>(L, "register_macro");
        setFunction<registerFilter>(L, "register_filter");
        setFunction<gettext>(L, "gettext");
        setFunction<log>(L, "log");
        setFunction<timeConversion<&Host::frameFromMs>>(L, "frame_from_ms");
        setFunction<timeConversion<&Host::msFromFrame>>(L, "ms_from_frame");
        setFunction<videoSize>(L, "video_size");
        setFunction<keyframes>(L, "keyframes");
        setFunction<properties>(L, "project_properties");
        setFunction<decodePath>(L, "decode_path");
        setFunction<fileName>(L, "file_name");
        setFunction<cancel>(L, "cancel");
        setFunction<setUndoPoint>(L, "set_undo_point");
        setFunction<textExtents>(L, "text_extents");
        lua_pushcfunction(L, LuaAssFile::parseKaraokeData);
        lua_setfield(L, -2, "parse_karaoke_data");
        setFunction<noValue>(L, "get_frame");
        setFunction<noValue>(L, "get_audio_selection");
        setFunction<progressText<&Host::progressTask>>(L, "set_status_text");

        lua_newtable(L);
        setFunction<log>(L, "out");
        lua_setfield(L, -2, "debug");
        lua_newtable(L);
        setFunction<progressSet>(L, "set");
        setFunction<progressText<&Host::progressTask>>(L, "task");
        setFunction<progressText<&Host::progressTitle>>(L, "title");
        setFunction<isCancelled>(L, "is_cancelled");
        lua_setfield(L, -2, "progress");
        lua_newtable(L);
        setFunction<displayDialog>(L, "display");
        setFunction<chooseFile<false>>(L, "open");
        setFunction<chooseFile<true>>(L, "save");
        lua_setfield(L, -2, "dialog");
        lua_setglobal(L, "aegisub");
        return true;
    }

    void beginCancellationHook()
    {
        // Compiled loops do not reliably dispatch instruction count hooks.
        for (const int ref : {disableJitRef, flushJitRef}) {
            if (ref == LUA_NOREF) continue;
            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
            if (lua_pcall(L, 0, 0, 0) != 0) lua_pop(L, 1);
        }
        lua_sethook(L, cancelHook, LUA_MASKCOUNT, 10000);
    }

    QString metadataValue(const char* name)
    {
        // Raw access avoids running a script-installed global __index.
        lua_pushstring(L, name);
        lua_rawget(L, LUA_GLOBALSINDEX);
        auto value = stringAt(L, -1);
        lua_pop(L, 1);
        return value;
    }

    template<typename Result, typename PushArgs, typename ReadResults>
    void call(int function, LuaAssFile& subs, int resultCount, Result& result,
              PushArgs&& pushArgs, ReadResults&& readResults)
    {
        const int base = lua_gettop(L);
        const auto cleanup = qScopeGuard([&] {
            lua_sethook(L, nullptr, 0, 0);
            subs.invalidate();
            lua_settop(L, base);
        });
        try {
            if (pollCancellation()) {
                result.cancelled = true;
                return;
            }
            lua_pushcfunction(L, luaCallback<agi::lua::add_stack_trace>);
            lua_rawgeti(L, LUA_REGISTRYINDEX, function);
            subs.push(L);
            const int extraArgs = pushArgs();
            beginCancellationHook();
            const int status = lua_pcall(L, 1 + extraArgs, resultCount, base + 1);
            lua_sethook(L, nullptr, 0, 0);
            if (pollCancellation()) result.cancelled = true;
            else if (status != 0) result.error = errorAtTop(L);
            else readResults();
        }
        catch (const agi::Exception& e) {
            result.error = QString::fromStdString(e.GetMessage());
        }
        catch (const std::exception& e) {
            result.error = QString::fromUtf8(e.what());
        }
    }

    template<typename ReadResults>
    void callMacro(int function, LuaAssFile& subs, const QVector<int>& selection, int active,
                   int resultCount, Outcome& result, ReadResults&& readResults)
    {
        call(function, subs, resultCount, result, [&] {
            pushIndexes(L, selection);
            if (active == 0 && !runningMacro) lua_pushnil(L);
            else lua_pushinteger(L, active);
            return 2;
        }, std::forward<ReadResults>(readResults));
    }
};

LuaScript::LuaScript() : state_(std::make_unique<State>()) {}
LuaScript::~LuaScript() = default;

bool LuaScript::load(const QString& filename, const QStringList& includeDirs, Host host, QString* error)
{
    if (state_->loading || state_->invoking) {
        if (error) *error = QStringLiteral("The Lua state is already running.");
        return false;
    }
    if (error) error->clear();
    state_ = std::make_unique<State>();
    auto& self = *state_;
    const QFileInfo file(filename);
    self.filename = file.absoluteFilePath();
    self.metadata.name = file.fileName();
    self.host = std::move(host);
    self.searchDirs = QStringList{file.absolutePath()};
    for (const auto& dir : includeDirs) {
        if (dir.isEmpty()) continue;
        const auto absolute = QDir::cleanPath(QDir(dir).absolutePath());
        if (!self.searchDirs.contains(absolute)) self.searchDirs.append(absolute);
    }
    const auto fail = [&](const QString& reason) {
        self.loading = false;
        if (self.L) {
            lua_close(self.L);
            self.L = nullptr;
        }
        self.macros.clear();
        self.macroFunctions.clear();
        self.filters.clear();
        self.filterFunctions.clear();
        if (error) *error = reason;
        return false;
    };

    self.L = luaL_newstate();
    if (!self.L) return fail(QStringLiteral("Could not initialize Lua."));
    lua_State* L = self.L;
    self.loading = true;
    if (!self.installApi()) return fail(errorAtTop(L));
    lua_pushcfunction(L, luaCallback<agi::lua::add_stack_trace>);
    self.beginCancellationHook();
    const bool loaded = !self.pollCancellation() && State::loadChunk(L, self.filename) && lua_pcall(L, 0, 0, 1) == 0;
    lua_sethook(L, nullptr, 0, 0);
    if (self.pollCancellation()) return fail(QStringLiteral("Lua script loading was cancelled."));
    if (!loaded) return fail(errorAtTop(L));
    lua_settop(L, 0);
    self.loading = false;

    lua_pushliteral(L, "version");
    lua_rawget(L, LUA_GLOBALSINDEX);
    if (lua_isnumber(L, -1) && lua_tointeger(L, -1) == 3)
        return fail(QStringLiteral("Automation 3 scripts are not supported."));
    lua_pop(L, 1);
    const auto name = self.metadataValue("script_name");
    if (!name.isEmpty()) self.metadata.name = name;
    self.metadata.description = self.metadataValue("script_description");
    self.metadata.author = self.metadataValue("script_author");
    self.metadata.version = self.metadataValue("script_version");
    return true;
}

const ScriptMetadata& LuaScript::metadata() const { return state_->metadata; }
const QVector<Macro>& LuaScript::macros() const { return state_->macros; }
const QVector<Filter>& LuaScript::filters() const { return state_->filters; }

Outcome LuaScript::validate(int macro, LuaAssFile& subs, const QVector<int>& selection, int active)
{
    Outcome result;
    auto& self = *state_;
    if (!self.L || self.loading || self.invoking || macro < 0 || macro >= self.macros.size()) {
        result.error = QStringLiteral("Lua macro is unavailable.");
        result.enabled = false;
        return result;
    }
    const State::Invocation invocation(self);
    const auto functions = self.macroFunctions[macro];
    result.description = self.macros[macro].description;
    if (functions.validate != LUA_NOREF) {
        self.callMacro(functions.validate, subs, selection, active, 3, result, [&] {
            result.enabled = lua_toboolean(self.L, -3);
            const auto description = stringAt(self.L, -2);
            if (!description.isEmpty()) result.description = description;
            if (lua_type(self.L, -1) == LUA_TSTRING) result.title = stringAt(self.L, -1);
        });
    }
    if (functions.isActive != LUA_NOREF && !result.cancelled && result.error.isEmpty()) {
        self.callMacro(functions.isActive, subs, selection, active, 1, result, [&] {
            result.checked = lua_toboolean(self.L, -1);
        });
    }
    if (result.cancelled || !result.error.isEmpty()) result.enabled = false;
    return result;
}

Outcome LuaScript::run(int macro, LuaAssFile& subs, const QVector<int>& selection, int active,
                       const Host* executionHost)
{
    Outcome result;
    auto& self = *state_;
    if (!self.L || self.loading || self.invoking || macro < 0 || macro >= self.macros.size()) {
        result.error = QStringLiteral("Lua macro is unavailable.");
        return result;
    }
    const State::Invocation invocation(self, executionHost, true);
    self.undoLabel = self.macros[macro].name;
    self.callMacro(self.macroFunctions[macro].run, subs, selection, active, 2, result, [&] {
        lua_State* L = self.L;
        if (!lua_isnil(L, -1)) {
            const auto index = integerAt(L, -1);
            if (!index || subs.eventOrdinal(*index) < 0) {
                result.error = QStringLiteral("The macro returned an invalid active line index.");
                return;
            }
            result.active = *index;
        }
        if (lua_isnil(L, -2)) return;
        if (!lua_istable(L, -2)) {
            result.error = QStringLiteral("The macro selection must be a table of line indexes.");
            return;
        }
        QVector<int> indexes;
        lua_pushnil(L);
        while (lua_next(L, -3)) {
            const auto index = integerAt(L, -1);
            if (!index || subs.eventOrdinal(*index) < 0) {
                result.error = QStringLiteral("The macro returned an invalid selected line index.");
                return;
            }
            indexes.append(*index);
            lua_pop(L, 1);
        }
        std::sort(indexes.begin(), indexes.end());
        indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
        result.selection = std::move(indexes);
    });
    if (!result.cancelled && result.error.isEmpty()) result.undoLabel = self.undoLabel;
    else {
        result.selection.reset();
        result.active.reset();
    }
    return result;
}

FilterConfiguration LuaScript::configureFilter(int index, LuaAssFile& subs, const QVariantMap& previous,
                                               const Host* executionHost)
{
    FilterConfiguration result;
    auto& self = *state_;
    if (!self.L || self.loading || self.invoking || index < 0 || index >= self.filters.size()) {
        result.error = QStringLiteral("Lua export filter is unavailable.");
        return result;
    }
    const int configure = self.filterFunctions[index].configure;
    if (configure == LUA_NOREF) return result;

    const State::Invocation invocation(self, executionHost);
    self.call(configure, subs, 1, result, [&] {
        pushMap(self.L, previous);
        return 1;
    }, [&] {
        result.dialog.insert(QStringLiteral("controls"), readControls(self.L, -1, previous));
    });
    return result;
}

Outcome LuaScript::runFilter(int index, LuaAssFile& subs, const QVariantMap& options,
                             const Host* executionHost)
{
    Outcome result;
    auto& self = *state_;
    if (!self.L || self.loading || self.invoking || index < 0 || index >= self.filters.size()) {
        result.error = QStringLiteral("Lua export filter is unavailable.");
        return result;
    }
    const State::Invocation invocation(self, executionHost);
    const auto& functions = self.filterFunctions[index];
    self.call(functions.run, subs, 0, result, [&] {
        if (functions.configure == LUA_NOREF) lua_newtable(self.L);
        else pushMap(self.L, options);
        return 1;
    }, [] {});
    return result;
}

} // namespace aegisub_qt::automation
