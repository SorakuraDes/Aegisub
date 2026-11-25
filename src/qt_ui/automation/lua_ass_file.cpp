#include "lua_ass_file.h"

#include "ass_attachment.h"
#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_info.h"
#include "ass_karaoke.h"
#include "ass_style.h"

#include <libaegisub/exception.h>
#include <libaegisub/lua/utils.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace aegisub_qt::automation {
namespace {

constexpr auto metatable = "Aegisub.Qt.Subtitles";
using Extra = std::map<std::string, std::string>;

struct Row {
    std::unique_ptr<AssEntry> entry;
    std::optional<Extra> extra;
};

// Do not call lua_error while a C++ exception or local object is live.
template<class F>
int guarded(lua_State* L, F f)
{
    try { return f(); }
    catch (const agi::Exception& e) { lua_pushstring(L, e.GetMessage().c_str()); }
    catch (const std::exception& e) { lua_pushstring(L, e.what()); }
    return lua_error(L);
}

void field(lua_State* L, const char* key, const std::string& value)
{
    lua_pushlstring(L, value.data(), value.size());
    lua_setfield(L, -2, key);
}

void field(lua_State* L, const char* key, const char* value)
{
    lua_pushstring(L, value);
    lua_setfield(L, -2, key);
}

void field(lua_State* L, const char* key, bool value)
{
    lua_pushboolean(L, value);
    lua_setfield(L, -2, key);
}

void field(lua_State* L, const char* key, double value)
{
    lua_pushnumber(L, value);
    lua_setfield(L, -2, key);
}

void field(lua_State* L, const char* key, int value)
{
    lua_pushinteger(L, value);
    lua_setfield(L, -2, key);
}

std::string stringAt(lua_State* L, int index)
{
    size_t size = 0;
    auto text = lua_tolstring(L, index, &size);
    if (!text) throw std::runtime_error("Expected a subtitle string");
    return {text, size};
}

int intAt(lua_State* L, int index)
{
    auto value = lua_tonumber(L, index);
    if (!lua_isnumber(L, index) || !std::isfinite(value) || std::trunc(value) != value ||
        value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
        throw std::runtime_error("Expected an integer subtitle index or field");
    return static_cast<int>(value);
}

void rawField(lua_State* L, int index, const char* name)
{
    lua_pushstring(L, name);
    lua_rawget(L, index);
}

template<class T>
T readField(lua_State* L, int index, const char* key)
{
    rawField(L, index, key);
    T value;
    if constexpr (std::is_same_v<T, std::string>) value = stringAt(L, -1);
    else if constexpr (std::is_same_v<T, int>) value = intAt(L, -1);
    else if constexpr (std::is_same_v<T, bool>) {
        if (!lua_isboolean(L, -1)) throw std::runtime_error(std::string("Expected a boolean field: ") + key);
        value = lua_toboolean(L, -1);
    }
    else {
        value = lua_tonumber(L, -1);
        if (!lua_isnumber(L, -1) || !std::isfinite(value))
            throw std::runtime_error(std::string("Expected a finite number field: ") + key);
    }
    lua_pop(L, 1);
    return value;
}

std::string lineClass(lua_State* L, int index)
{
    if (!lua_istable(L, index)) throw std::runtime_error("Expected a subtitle line table");
    auto name = readField<std::string>(L, index, "class");
    for (char& c : name)
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return name;
}

Extra extraAt(lua_State* L, int index)
{
    Extra result;
    rawField(L, index, "extra");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            if (lua_type(L, -2) == LUA_TSTRING)
                result.emplace(stringAt(L, -2), stringAt(L, -1));
            lua_pop(L, 1);
        }
    }
    else if (!lua_isnil(L, -1)) throw std::runtime_error("Dialogue extradata must be a table");
    lua_pop(L, 1);
    return result;
}

Row readRow(lua_State* L, int i, const AssEntry* previous = nullptr)
{
    auto kind = lineClass(L, i);
    auto s = [&](const char* key) { return readField<std::string>(L, i, key); };
    auto n = [&](const char* key) { return readField<int>(L, i, key); };
    auto b = [&](const char* key) { return readField<bool>(L, i, key); };
    auto d = [&](const char* key) { return readField<double>(L, i, key); };
    if (kind == "info") return {std::make_unique<AssInfo>(s("key"), s("value")), {}};
    if (kind == "dialogue") {
        auto old = dynamic_cast<const AssDialogue*>(previous);
        auto line = old ? std::make_unique<AssDialogue>(static_cast<const AssDialogueBase&>(*old))
                        : std::make_unique<AssDialogue>();
        line->Comment = b("comment");
        line->Layer = n("layer");
        const auto start = n("start_time");
        const auto end = n("end_time");
        // Lua sees the native rounded time. Preserve any hidden precision
        // when only another field of the dialogue was edited.
        if (!old || start != static_cast<int>(old->Start)) line->Start = start;
        if (!old || end != static_cast<int>(old->End)) line->End = end;
        line->Style = s("style");
        line->Actor = s("actor");
        line->Effect = s("effect");
        line->Text = s("text");
        line->Margin = {n("margin_l"), n("margin_r"), n("margin_t")};
        return {std::move(line), extraAt(L, i)};
    }
    if (kind == "style") {
        auto style = std::make_unique<AssStyle>();
        style->name = s("name");
        style->font = s("fontname");
        style->fontsize = d("fontsize");
        style->primary = s("color1");
        style->secondary = s("color2");
        style->outline = s("color3");
        style->shadow = s("color4");
        style->bold = b("bold");
        style->italic = b("italic");
        style->underline = b("underline");
        style->strikeout = b("strikeout");
        style->scalex = d("scale_x");
        style->scaley = d("scale_y");
        style->spacing = d("spacing");
        style->angle = d("angle");
        style->borderstyle = n("borderstyle");
        style->outline_w = d("outline");
        style->shadow_w = d("shadow");
        style->alignment = n("align");
        style->Margin = {n("margin_l"), n("margin_r"), n("margin_t")};
        style->encoding = n("encoding");
        style->UpdateData();
        return {std::move(style), {}};
    }
    throw std::runtime_error("Unknown subtitle class: " + kind);
}

Extra storedExtra(const AssFile& file, const AssDialogue& line)
{
    Extra result;
    for (const auto& data : file.GetExtradata(line.ExtradataIds)) result[data.key] = data.value;
    return result;
}

void writeRow(lua_State* L, const Row& row, const AssFile& file)
{
    auto& entry = *row.entry;
    lua_newtable(L);
    field(L, "section", entry.GroupHeader());
    auto margins = [&](const auto& line) {
        field(L, "margin_l", line.Margin[0]);
        field(L, "margin_r", line.Margin[1]);
        field(L, "margin_t", line.Margin[2]);
        field(L, "margin_b", line.Margin[2]);
    };
    if (auto line = dynamic_cast<const AssDialogue*>(&entry)) {
        field(L, "class", "dialogue");
        field(L, "raw", line->GetEntryData());
        field(L, "comment", line->Comment);
        field(L, "layer", line->Layer);
        field(L, "start_time", static_cast<int>(line->Start));
        field(L, "end_time", static_cast<int>(line->End));
        field(L, "style", line->Style.get());
        field(L, "actor", line->Actor.get());
        field(L, "effect", line->Effect.get());
        field(L, "text", line->Text.get());
        margins(*line);
        lua_newtable(L);
        auto add = [&](const auto& key, const auto& value) {
            lua_pushlstring(L, key.data(), key.size());
            lua_pushlstring(L, value.data(), value.size());
            lua_rawset(L, -3);
        };
        if (row.extra) {
            for (const auto& [key, value] : *row.extra) add(key, value);
        }
        else {
            for (const auto& data : file.GetExtradata(line->ExtradataIds)) add(data.key, data.value);
        }
        lua_setfield(L, -2, "extra");
    }
    else if (auto style = dynamic_cast<const AssStyle*>(&entry)) {
        field(L, "class", "style");
        field(L, "raw", style->GetEntryData());
        field(L, "name", style->name);
        field(L, "fontname", style->font);
        field(L, "fontsize", style->fontsize);
        field(L, "color1", style->primary.GetAssStyleFormatted() + "&");
        field(L, "color2", style->secondary.GetAssStyleFormatted() + "&");
        field(L, "color3", style->outline.GetAssStyleFormatted() + "&");
        field(L, "color4", style->shadow.GetAssStyleFormatted() + "&");
        field(L, "bold", style->bold);
        field(L, "italic", style->italic);
        field(L, "underline", style->underline);
        field(L, "strikeout", style->strikeout);
        field(L, "scale_x", style->scalex);
        field(L, "scale_y", style->scaley);
        field(L, "spacing", style->spacing);
        field(L, "angle", style->angle);
        field(L, "borderstyle", style->borderstyle);
        field(L, "outline", style->outline_w);
        field(L, "shadow", style->shadow_w);
        field(L, "align", style->alignment);
        field(L, "encoding", style->encoding);
        field(L, "relative_to", 2);
        margins(*style);
    }
    else {
        const auto& info = static_cast<const AssInfo&>(entry);
        field(L, "class", "info");
        field(L, "raw", info.GetEntryData());
        field(L, "key", info.Key());
        field(L, "value", info.Value());
    }
}

} // namespace

struct LuaAssFile::State {
    struct Token { State* owner; };
    struct Userdata { std::shared_ptr<Token>* token; };

    const AssFile* source;
    const bool writable;
    const int offset;
    bool published = false;
    std::shared_ptr<Token> token;
    std::vector<Row> rows;
    mutable bool ordinalDirty = true;
    mutable std::vector<int> ordinals;
    // Used for local lookup during Lua execution, then for retired live nodes.
    AssFile data;

    State(const AssFile& file, bool writable)
        : source(&file), writable(writable), offset(static_cast<int>(file.Info.size() + file.Styles.size()))
    {
        data.Extradata = file.Extradata;
        data.next_extradata_id = file.next_extradata_id;
        rows.reserve(static_cast<size_t>(offset) + file.Events.size());
        for (const auto& info : file.Info) rows.push_back({std::make_unique<AssInfo>(info), {}});
        for (const auto& style : file.Styles) rows.push_back({std::make_unique<AssStyle>(style), {}});
        for (const auto& line : file.Events) {
            auto copy = std::make_unique<AssDialogue>(static_cast<const AssDialogueBase&>(line));
            copy->Fold = {};
            rows.push_back({std::move(copy), {}});
        }
    }

    static State& get(lua_State* L, int index)
    {
        if (lua_type(L, index) != LUA_TUSERDATA || lua_objlen(L, index) != sizeof(Userdata) ||
            !lua_getmetatable(L, index)) throw std::runtime_error("Expected subtitles object");
        luaL_getmetatable(L, metatable);
        bool same = lua_rawequal(L, -1, -2);
        lua_pop(L, 2);
        if (!same) throw std::runtime_error("Expected subtitles object");
        auto ud = static_cast<Userdata*>(lua_touserdata(L, index));
        if (!ud->token || !(*ud->token)->owner) throw std::runtime_error("Subtitles object is no longer valid");
        return *(*ud->token)->owner;
    }

    static int gc(lua_State* L)
    {
        if (lua_type(L, 1) == LUA_TUSERDATA && lua_objlen(L, 1) == sizeof(Userdata) && lua_getmetatable(L, 1)) {
            luaL_getmetatable(L, metatable);
            const bool same = lua_rawequal(L, -1, -2);
            lua_pop(L, 2);
            if (same) {
                auto ud = static_cast<Userdata*>(lua_touserdata(L, 1));
                delete std::exchange(ud->token, nullptr);
            }
        }
        return 0;
    }

    void checkWritable() const
    {
        if (!writable || published) throw std::runtime_error("Subtitles object is read-only");
    }

    size_t checkedIndex(int index, bool insertion = false) const
    {
        if (index <= 0 || static_cast<size_t>(index) > rows.size() + (insertion ? 1 : 0))
            throw std::runtime_error("Out of range subtitle line index");
        return static_cast<size_t>(index - 1);
    }

    void add(lua_State* L, int first, std::optional<size_t> before)
    {
        std::vector<Row> additions;
        for (int i = first; i <= lua_gettop(L); ++i) additions.push_back(readRow(L, i));
        if (additions.empty()) return;
        ordinalDirty = true;
        const auto required = rows.size() + additions.size();
        if (required > rows.capacity()) rows.reserve(std::max(required, rows.capacity() + rows.capacity() / 2));
        if (before && *before != rows.size()) {
            rows.insert(rows.begin() + *before, std::make_move_iterator(additions.begin()),
                        std::make_move_iterator(additions.end()));
            return;
        }
        // Native append inserts after the last row of the same class, or at EOF.
        for (auto& row : additions) {
            const auto group = row.entry->Group();
            auto last = std::find_if(rows.rbegin(), rows.rend(), [group](const Row& r) { return r.entry->Group() == group; });
            rows.insert(last == rows.rend() ? rows.end() : last.base(), std::move(row));
        }
    }

    static int len(lua_State* L)
    {
        return guarded(L, [&] {
            lua_pushinteger(L, get(L, 1).rows.size());
            return 1;
        });
    }

    static int next(lua_State* L)
    {
        return guarded(L, [&] {
            auto& self = get(L, lua_upvalueindex(1));
            auto index = intAt(L, 2);
            if (index < 0 || static_cast<size_t>(index) >= self.rows.size()) return 0;
            lua_pushinteger(L, static_cast<lua_Integer>(index) + 1);
            writeRow(L, self.rows[index], self.data);
            return 2;
        });
    }

    static int iterate(lua_State* L)
    {
        return guarded(L, [&] {
            get(L, 1);
            lua_pushvalue(L, 1);
            lua_pushcclosure(L, next, 1);
            lua_pushnil(L);
            lua_pushinteger(L, 0);
            return 3;
        });
    }

    enum class Action { Append, Insert, Delete, DeleteRange, Resolution };

    template<Action action>
    static int method(lua_State* L)
    {
        return guarded(L, [&] {
            auto& self = get(L, lua_upvalueindex(1));
            const int first = lua_rawequal(L, 1, lua_upvalueindex(1)) ? 2 : 1;
            if constexpr (action == Action::Resolution) {
                AssFile info;
                for (const auto& row : self.rows)
                    if (row.entry->Group() == AssEntryGroup::INFO)
                        info.Info.push_back(static_cast<const AssInfo&>(*row.entry));
                int w, h;
                info.GetResolution(w, h);
                lua_pushinteger(L, w);
                lua_pushinteger(L, h);
                return 2;
            }
            self.checkWritable();
            if constexpr (action == Action::Append) self.add(L, first, {});
            if constexpr (action == Action::Insert) self.add(L, first + 1, self.checkedIndex(intAt(L, first), true));
            if constexpr (action == Action::DeleteRange) {
                auto start = static_cast<size_t>(std::max(1, intAt(L, first)) - 1);
                auto end = std::min(self.rows.size(), static_cast<size_t>(std::max(0, intAt(L, first + 1))));
                if (start < end) {
                    self.rows.erase(self.rows.begin() + start, self.rows.begin() + end);
                    self.ordinalDirty = true;
                }
            }
            if constexpr (action == Action::Delete) {
                std::vector<size_t> indexes;
                if (first == lua_gettop(L) && lua_istable(L, first)) {
                    lua_pushnil(L);
                    while (lua_next(L, first)) {
                        indexes.push_back(self.checkedIndex(intAt(L, -1)));
                        lua_pop(L, 1);
                    }
                }
                else {
                    for (int i = first; i <= lua_gettop(L); ++i) indexes.push_back(self.checkedIndex(intAt(L, i)));
                }
                if (!indexes.empty()) {
                    std::sort(indexes.begin(), indexes.end());
                    indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
                    auto remove = indexes.begin();
                    size_t pos = 0;
                    auto end = std::remove_if(self.rows.begin(), self.rows.end(), [&](const Row&) {
                        if (remove != indexes.end() && *remove == pos++) { ++remove; return true; }
                        return false;
                    });
                    self.rows.erase(end, self.rows.end());
                    self.ordinalDirty = true;
                }
            }
            return 0;
        });
    }

    static int index(lua_State* L)
    {
        return guarded(L, [&] {
            auto& self = get(L, 1);
            if (lua_type(L, 2) == LUA_TNUMBER) {
                writeRow(L, self.rows[self.checkedIndex(intAt(L, 2))], self.data);
                return 1;
            }
            auto key = stringAt(L, 2);
            if (key == "n") return len(L);
            lua_CFunction func;
            if (key == "append") func = method<Action::Append>;
            else if (key == "insert") func = method<Action::Insert>;
            else if (key == "delete") func = method<Action::Delete>;
            else if (key == "deleterange") func = method<Action::DeleteRange>;
            else if (key == "script_resolution") func = method<Action::Resolution>;
            else throw std::runtime_error("Unknown subtitles property: " + key);
            lua_pushvalue(L, 1);
            lua_pushcclosure(L, func, 1);
            return 1;
        });
    }

    static int newindex(lua_State* L)
    {
        return guarded(L, [&] {
            auto& self = get(L, 1);
            self.checkWritable();
            const int n = intAt(L, 2);
            if (n == std::numeric_limits<int>::min()) throw std::runtime_error("Out of range subtitle line index");
            if (n < 0) self.add(L, 3, self.checkedIndex(-n, true));
            else if (!n) self.add(L, 3, {});
            else {
                auto i = self.checkedIndex(n);
                if (lua_isnil(L, 3)) self.rows.erase(self.rows.begin() + i);
                else {
                    auto replacement = readRow(L, 3, self.rows[i].entry.get());
                    if (replacement.entry->Group() != self.rows[i].entry->Group()) self.ordinalDirty = true;
                    self.rows[i] = std::move(replacement);
                    return 0;
                }
                self.ordinalDirty = true;
            }
            return 0;
        });
    }
};

LuaAssFile::LuaAssFile(const AssFile& source, bool writable)
    : state_(std::make_unique<State>(source, writable))
{
}

LuaAssFile::~LuaAssFile() { invalidate(); }

void LuaAssFile::push(lua_State* L)
{
    invalidate();
    state_->token = std::make_shared<State::Token>(State::Token{state_->published ? nullptr : state_.get()});
    if (luaL_newmetatable(L, metatable)) {
        const luaL_Reg functions[] = {
            {"__index", State::index}, {"__newindex", State::newindex}, {"__len", State::len},
            {"__ipairs", State::iterate}, {"__pairs", State::iterate}, {"__gc", State::gc}, {nullptr, nullptr}
        };
        luaL_register(L, nullptr, functions);
        lua_pushliteral(L, "Aegisub subtitles");
        lua_setfield(L, -2, "__metatable");
    }
    auto ud = new (lua_newuserdata(L, sizeof(State::Userdata))) State::Userdata{nullptr};
    lua_insert(L, -2);
    lua_setmetatable(L, -2);
    ud->token = new std::shared_ptr<State::Token>(state_->token);
}

void LuaAssFile::invalidate() noexcept
{
    if (state_->token) {
        state_->token->owner = nullptr;
        state_->token.reset();
    }
}

int LuaAssFile::initialEventOffset() const noexcept { return state_->offset; }

int LuaAssFile::eventOrdinal(int absoluteIndex) const
{
    auto& self = *state_;
    if (absoluteIndex <= 0 || static_cast<size_t>(absoluteIndex) > self.rows.size()) return -1;
    if (self.ordinalDirty) {
        self.ordinals.clear();
        self.ordinals.reserve(self.rows.size());
        int ordinal = 0;
        for (const auto& row : self.rows)
            self.ordinals.push_back(row.entry->Group() == AssEntryGroup::DIALOGUE ? ordinal++ : -1);
        self.ordinalDirty = false;
    }
    return self.ordinals[absoluteIndex - 1];
}

namespace {

auto styleFields(const AssStyle& s)
{
    return std::tie(s.name, s.font, s.fontsize, s.primary, s.secondary, s.outline, s.shadow,
                    s.bold, s.italic, s.underline, s.strikeout, s.scalex, s.scaley, s.spacing,
                    s.angle, s.borderstyle, s.outline_w, s.shadow_w, s.alignment, s.Margin, s.encoding);
}

auto eventMeta(const AssDialogue& d)
{
    return std::tie(d.Comment, d.Layer, d.Margin, d.Style.get(), d.Actor.get(), d.Effect.get());
}

} // namespace

int LuaAssFile::publish(AssFile& destination)
{
    auto& self = *state_;
    if (!self.writable || self.published || self.source != &destination) return 0;

    // Prepare all allocations and compute exact data changes before touching
    // the live document. Extra values in removed/transient rows never get IDs.
    AssFile next;
    auto currentInfo = destination.Info.begin();
    auto currentStyle = destination.Styles.begin();
    auto currentEvent = destination.Events.begin();
    int flags = 0;
    int eventRow = 0;
    for (auto& row : self.rows) {
        switch (row.entry->Group()) {
        case AssEntryGroup::INFO: {
            const auto& src = static_cast<const AssInfo&>(*row.entry);
            if (currentInfo == destination.Info.end()) flags |= AssFile::COMMIT_SCRIPTINFO;
            else {
                if (src.Key() != currentInfo->Key() || src.Value() != currentInfo->Value())
                    flags |= AssFile::COMMIT_SCRIPTINFO;
                ++currentInfo;
            }
            next.Info.push_back(src);
            break;
        }
        case AssEntryGroup::STYLE: {
            const auto& src = static_cast<const AssStyle&>(*row.entry);
            auto chosen = &src;
            if (currentStyle == destination.Styles.end()) flags |= AssFile::COMMIT_STYLES;
            else {
                if (styleFields(src) != styleFields(*currentStyle)) flags |= AssFile::COMMIT_STYLES;
                else chosen = &*currentStyle; // retain unchanged cached raw data
                ++currentStyle;
            }
            auto copy = std::make_unique<AssStyle>(*chosen);
            next.Styles.push_back(*copy.release());
            break;
        }
        case AssEntryGroup::DIALOGUE: {
            const auto& src = static_cast<const AssDialogue&>(*row.entry);
            auto copy = std::make_unique<AssDialogue>(static_cast<const AssDialogueBase&>(src));
            copy->Row = eventRow++;
            if (row.extra && *row.extra != storedExtra(self.data, src)) {
                if (next.Extradata.empty() && next.next_extradata_id == 0) {
                    next.Extradata = destination.Extradata;
                    next.next_extradata_id = destination.next_extradata_id;
                }
                std::vector<uint32_t> ids;
                for (const auto& [key, value] : *row.extra) {
                    auto found = std::find_if(src.ExtradataIds.get().begin(), src.ExtradataIds.get().end(), [&](uint32_t id) {
                        auto it = std::lower_bound(next.Extradata.begin(), next.Extradata.end(), id,
                            [](const auto& data, uint32_t value) { return data.id < value; });
                        return it != next.Extradata.end() && it->id == id && it->key == key && it->value == value;
                    });
                    ids.push_back(found == src.ExtradataIds.get().end() ? next.AddExtradata(key, value) : *found);
                }
                std::sort(ids.begin(), ids.end());
                copy->ExtradataIds = std::move(ids);
            }
            if (currentEvent == destination.Events.end()) flags |= AssFile::COMMIT_DIAG_ADDREM;
            else {
                if (eventMeta(*copy) != eventMeta(*currentEvent)) flags |= AssFile::COMMIT_DIAG_META;
                if (copy->Start != currentEvent->Start || copy->End != currentEvent->End) flags |= AssFile::COMMIT_DIAG_TIME;
                if (copy->Text.get() != currentEvent->Text.get()) flags |= AssFile::COMMIT_DIAG_TEXT;
                if (copy->ExtradataIds.get() != currentEvent->ExtradataIds.get()) flags |= AssFile::COMMIT_EXTRADATA;
                ++currentEvent;
            }
            next.Events.push_back(*copy.release());
            break;
        }
        default: break;
        }
    }
    if (currentInfo != destination.Info.end()) flags |= AssFile::COMMIT_SCRIPTINFO;
    if (currentStyle != destination.Styles.end()) flags |= AssFile::COMMIT_STYLES;
    if (currentEvent != destination.Events.end()) flags |= AssFile::COMMIT_DIAG_ADDREM;
    const bool publishData = next.Extradata.size() > destination.Extradata.size();
    if (publishData) flags |= AssFile::COMMIT_EXTRADATA;

    self.published = true;
    invalidate();
    if (flags & AssFile::COMMIT_SCRIPTINFO) {
        destination.Info.swap(next.Info);
        self.data.Info.swap(next.Info);
    }
    if (flags & AssFile::COMMIT_STYLES) {
        destination.Styles.swap(next.Styles);
        self.data.Styles.swap(next.Styles);
    }
    if (flags & (AssFile::COMMIT_DIAG_FULL | AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_EXTRADATA)) {
        destination.Events.swap(next.Events);
        self.data.Events.swap(next.Events);
        // Every event address changed; notify listeners caching event pointers.
        flags |= AssFile::COMMIT_DIAG_ADDREM;
    }
    if (publishData) {
        destination.Extradata.swap(next.Extradata);
        self.data.Extradata.swap(next.Extradata);
        destination.next_extradata_id = next.next_extradata_id;
    }
    return flags;
}

int LuaAssFile::parseKaraokeData(lua_State* L)
{
    return guarded(L, [&] {
        if (lineClass(L, 1) != "dialogue") throw std::runtime_error("Subtitle line must be a dialogue line");
        AssDialogue line;
        line.Start = readField<int>(L, 1, "start_time");
        line.End = readField<int>(L, 1, "end_time");
        line.Text = readField<std::string>(L, 1, "text");
        AssKaraoke karaoke(&line, false, false);
        lua_newtable(L);
        auto push = [&](int index, int start, int duration, const auto& tag, const auto& text, const auto& stripped) {
            lua_createtable(L, 0, 6);
            field(L, "duration", duration);
            field(L, "start_time", start);
            field(L, "end_time", static_cast<double>(start) + duration);
            field(L, "tag", tag);
            field(L, "text", text);
            field(L, "text_stripped", stripped);
            lua_rawseti(L, -2, index);
        };
        push(0, 0, 0, "", "", "");
        int index = 1;
        for (const auto& syl : karaoke)
            push(index++, syl.start_time - static_cast<int>(line.Start), syl.duration, syl.tag_type, syl.GetText(false), syl.text);
        return 1;
    });
}

} // namespace aegisub_qt::automation
