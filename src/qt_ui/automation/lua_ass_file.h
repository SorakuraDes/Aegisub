#pragma once
#include <memory>

class AssFile;
struct lua_State;

namespace aegisub_qt::automation {

// GUI-thread-only transaction. Keep alive through commit to retain displaced nodes.
class LuaAssFile {
public:
    LuaAssFile(const AssFile& source, bool writable);
    ~LuaAssFile();
    LuaAssFile(const LuaAssFile&) = delete;
    LuaAssFile& operator=(const LuaAssFile&) = delete;

    void push(lua_State* L);
    void invalidate() noexcept;
    static int parseKaraokeData(lua_State* L);

    int initialEventOffset() const noexcept;
    // Current 1-based Lua index to 0-based event ordinal; -1 for non-events.
    int eventOrdinal(int absoluteIndex) const;
    // Return commit flags (or zero); does not emit signals.
    int publish(AssFile& destination);

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace aegisub_qt::automation
