#pragma once

#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

class ISchemaSystem;

namespace slow_animation {

struct PlayerLayout {
    int connected = -1;
    int flags = -1;
    int hltv = -1;
    bool Ready() const { return connected >= 0 && flags >= 0 && hltv >= 0; }
};

PlayerLayout ResolvePlayerLayout(ISchemaSystem* schema);

template<class Value>
bool ReadField(const void* entity, int offset, Value& output) {
    static_assert(std::is_trivially_copyable_v<Value>);
    if (!entity || offset < 0 || offset > std::numeric_limits<int>::max() - int(sizeof(Value))) return false;
    // A missing field must never turn into entity-1. memcpy also avoids
    // alignment/aliasing UB for fields resolved through the live schema.
    std::memcpy(&output, static_cast<const unsigned char*>(entity) + offset, sizeof(Value));
    return true;
}

inline bool ReadHuman(const void* controller, const PlayerLayout& layout,
                      std::uint32_t fakeClientFlag, bool& human) {
    human = false;
    if (!controller || !layout.Ready()) return false;
    std::uint32_t connected = 0, flags = 0;
    bool hltv = false;
    if (!ReadField(controller, layout.connected, connected) ||
        !ReadField(controller, layout.flags, flags) ||
        !ReadField(controller, layout.hltv, hltv)) return false;
    human = connected == 0 && !(flags & fakeClientFlag) && !hltv;
    return true;
}

} // namespace slow_animation
