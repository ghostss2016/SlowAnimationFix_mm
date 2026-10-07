#include "player_layout.h"
#include <schemasystem/schemasystem.h>
#include <cstring>

namespace slow_animation {

static int Field(CSchemaSystemTypeScope* scope, const char* className, const char* fieldName, int width) {
    if (!scope || !className || !fieldName) return -1;
    const auto* info = scope->FindDeclaredClass(className).Get();
    if (!info || !info->m_pFields || !info->m_nFieldCount || width <= 0 || info->m_nSize < width) return -1;
    for (int i = 0; i < info->m_nFieldCount; ++i) {
        const auto& field = info->m_pFields[i];
        if (field.m_pszName && !std::strcmp(field.m_pszName, fieldName)) {
            const int offset = field.m_nSingleInheritanceOffset;
            return offset >= 0 && offset <= info->m_nSize - width ? offset : -1;
        }
    }
    return -1;
}

PlayerLayout ResolvePlayerLayout(ISchemaSystem* schema) {
    if (!schema) return {};
#ifdef _WIN32
    auto* scope = schema->FindTypeScopeForModule("server.dll");
#else
    auto* scope = schema->FindTypeScopeForModule("libserver.so");
#endif
    if (!scope) return {};
    return {Field(scope, "CBasePlayerController", "m_iConnected", sizeof(std::uint32_t)),
            Field(scope, "CBaseEntity", "m_fFlags", sizeof(std::uint32_t)),
            Field(scope, "CBasePlayerController", "m_bIsHLTV", sizeof(bool))};
}

} // namespace slow_animation
