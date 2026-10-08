// Author: Michal Přikryl (Slynx) <github.com/SlynxCZ>
// SVAROG fork: official MetaMod API18, checked schema and owned lifecycle.
#include "plugin.h"
#include <variant.h>
#include <const.h>
#include <vector>
#include <menus.h> // canonical cs2-utils/include/menus.h, unchanged IUtilsApi ABI
#include <entitysystem.h>
#include <IPluginManager.h>
#include <icvar.h>
#include <interfaces/interfaces.h>
#include <tier1/convar.h>
#include <chrono>
#include <cstdio>

Plugin g_Plugin;
PLUGIN_EXPOSE(Plugin, g_Plugin);

// The SDK entitysystem/entityidentity implementation expects the embedding
// module to supply this accessor. Resolve it inside this SO through the owned
// Utils API, not a private engine offset or another plugin's exported symbol.
CGameEntitySystem* GameEntitySystem() {
    return g_Plugin.EntitySystem();
}

namespace {
double WallNow() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct CallbackScope {
    std::atomic<unsigned>& active;
    explicit CallbackScope(std::atomic<unsigned>& count) : active(count) { ++active; }
    ~CallbackScope() { --active; }
};
}

bool Plugin::ConVarApi::Available() { return g_pCVar != nullptr; }

std::unique_ptr<Plugin::ConVarApi::Reference> Plugin::ConVarApi::Create(const char* name) {
    return std::make_unique<Reference>(name);
}

void Plugin::ConVarApi::Register() { META_CONVAR_REGISTER(FCVAR_NONE); }

void Plugin::ConVarApi::Unregister() {
    // convar.cpp is linked into this hidden/export-mapped module; this cleans
    // its own registry, not the provider's engine cvar or another plugin's list.
    ConVar_Unregister();
}

bool Plugin::ReadFile(const char* path, std::string& output) {
    if (!filesystem_) return false;
    const auto file = filesystem_->Open(path, "r", "GAME");
    if (file == FILESYSTEM_INVALID_HANDLE) return false;
    const auto size = filesystem_->Size(file);
    if (!size || size > 65536) {
        filesystem_->Close(file);
        return false;
    }
    std::string text(size, '\0');
    const int read = filesystem_->Read(text.data(), static_cast<int>(size), file);
    filesystem_->Close(file);
    if (read != static_cast<int>(size)) return false;
    output = std::move(text);
    return true;
}

bool Plugin::ReadConfiguration() {
    std::string text;
    slow_animation::Settings next;
    if (!ReadFile("addons/slow_animation_fix/slow_animation_fix.ini", text) ||
        !slow_animation::ParseSettings(text, next)) return false;
    std::map<std::string, std::string> messages;
    const auto translations = "addons/slow_animation_fix/translations/" + next.language + ".ini";
    if (!ReadFile(translations.c_str(), text) || !slow_animation::ParseEntries(text, messages)) return false;
    for (const auto* key : {"config_invalid", "schema_unavailable", "utils_unavailable",
                           "hooks_unavailable", "cvar_unavailable", "unload_busy", "map_invalid", "reloading", "restoring"})
        if (!messages.count(key)) return false;
    settings_ = std::move(next);
    messages_ = std::move(messages);
    runtime_.Reconfigure(settings_);
    return true;
}

const char* Plugin::Message(const char* key) const {
    const auto it = messages_.find(key);
    return it == messages_.end() ? key : it->second.c_str();
}

void Plugin::Log(const char* key) { META_LOG(this, "%s", Message(key)); }

bool Plugin::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool) {
    PLUGIN_SAVEVARS(); // obtains the official API18 KHook interface
    GET_V_IFACE_CURRENT(GetServerFactory, server_, ISource2Server, INTERFACEVERSION_SERVERGAMEDLL);
    GET_V_IFACE_CURRENT(GetEngineFactory, engine_, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, schema_, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, network_, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetFileSystemFactory, filesystem_, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
    if (!ReadConfiguration()) {
        // Bootstrap diagnostic is the key: no implicit mechanic defaults and
        // no hardcoded fallback language when the package is incomplete.
        g_SMAPI->Format(error, maxlen, "%s", Message("config_invalid"));
        return false;
    }
    if (!KHook::__exported__khook) {
        g_SMAPI->Format(error, maxlen, "%s", Message("hooks_unavailable"));
        return false;
    }
    auto convarLoad = timelimit_.RollbackUnlessCommitted();
    if (!timelimit_.Acquire("mp_timelimit")) {
        g_SMAPI->Format(error, maxlen, "%s", Message("cvar_unavailable"));
        return false;
    }
    frameHook_ = std::make_unique<FrameHook>(&ISource2Server::GameFrame, this, nullptr, &Plugin::Hook_GameFrame);
    startupHook_ = std::make_unique<StartupHook>(&INetworkServerService::StartupServer, this, nullptr, &Plugin::Hook_StartupServer);
    if (!frameHook_->AddInstance(server_) || !startupHook_->AddInstance(network_)) {
        startupHook_.reset();
        frameHook_.reset();
        g_SMAPI->Format(error, maxlen, "%s", Message("hooks_unavailable"));
        return false;
    }
    runtime_.Start(settings_, WallNow());
    loaded_ = true;
    g_SMAPI->AddListener(this, this);
    convarLoad.Commit();
    return true;
}

void Plugin::BindUtils() {
    if (!loaded_ || utils_) return;
    int result = 0;
    PluginId owner = 0;
    auto* api = static_cast<IUtilsApi*>(g_SMAPI->MetaFactory(Utils_INTERFACE, &result, &owner));
    if (!api || result != META_IFACE_OK || owner <= 0) return;
    utils_ = api;
    utilsOwner_ = owner;
}

CGameEntitySystem* Plugin::EntitySystem() const {
    if (!loaded_ || !utils_ || utilsOwner_ <= 0 || !g_SMAPI) return nullptr;
    auto* manager = static_cast<SourceMM::ISmmPluginManager*>(
        g_SMAPI->MetaFactory(MMIFACE_PLMANAGER, nullptr, nullptr));
    SourceMM::Pl_Status status = SourceMM::Pl_NotFound;
    if (!manager || !manager->Query(utilsOwner_, nullptr, &status, nullptr) || status != SourceMM::Pl_Running)
        return nullptr;
    int result = 0;
    PluginId owner = 0;
    auto* current = static_cast<IUtilsApi*>(g_SMAPI->MetaFactory(Utils_INTERFACE, &result, &owner));
    if (current != utils_ || result != META_IFACE_OK || owner != utilsOwner_) return nullptr;
    return utils_->GetCGameEntitySystem();
}

void Plugin::AllPluginsLoaded() {
    BindUtils();
    if (!utils_) Log("utils_unavailable");
    // Covers a late load without guessing entity-service memory offsets.
    if (runtime_.Map().empty()) {
        auto* game = network_ ? network_->GetIGameServer() : nullptr;
        if (game && game->GetMapName()) StartMap(game->GetMapName());
    }
}

void Plugin::StartMap(const char* mapName) {
    if (!ReadConfiguration()) Log("config_invalid"); // retain last valid config
    layout_ = slow_animation::ResolvePlayerLayout(schema_);
    if (!layout_.Ready()) Log("schema_unavailable");
    if (!slow_animation::ValidMap(mapName)) {
        runtime_.MapStart(nullptr, WallNow());
        Log("map_invalid");
        return;
    }
    runtime_.MapStart(mapName, WallNow());
}

KHook::Return<void> Plugin::Hook_GameFrame(ISource2Server*, bool, bool, bool) {
    CallbackScope scope(callbacks_);
    Backend backend{*this};
    runtime_.Frame(WallNow(), backend);
    return {KHook::Action::Ignore};
}

KHook::Return<void> Plugin::Hook_StartupServer(INetworkServerService*,
    const GameSessionConfiguration_t&, ISource2WorldSession*, const char* mapName) {
    CallbackScope scope(callbacks_);
    StartMap(mapName);
    return {KHook::Action::Ignore};
}

void Plugin::OnPluginLoad(PluginId) { BindUtils(); }
void Plugin::OnPluginUnpause(PluginId) { BindUtils(); }
void Plugin::OnPluginPause(PluginId id) { OnPluginUnload(id); }

void Plugin::OnPluginUnload(PluginId id) {
    if (id != utilsOwner_ || utilsOwner_ == 0) return;
    // Do not call an unloading provider, retain entity pointers, or leave a
    // restoration belonging to its previous lifetime queued in our module.
    utils_ = nullptr;
    utilsOwner_ = 0;
    runtime_.DependencyLost();
}

void Plugin::OnLevelShutdown() {
    runtime_.MapShutdown();
    layout_ = {};
}

bool Plugin::Unload(char* error, size_t maxlen) {
    if (callbacks_.load() || ((frameHook_ || startupHook_) && !KHook::__exported__khook)) {
        g_SMAPI->Format(error, maxlen, "%s", Message("unload_busy"));
        return false;
    }
    loaded_ = false;
    runtime_.Shutdown();
    utils_ = nullptr;
    utilsOwner_ = 0;
    layout_ = {};
    startupHook_.reset();
    frameHook_.reset();
    timelimit_.Reset();
    return true;
}

bool Plugin::Backend::Ready() const {
    return plugin.loaded_ && plugin.utils_ && plugin.engine_ && plugin.layout_.Ready();
}

int Plugin::Backend::HumanCount() const {
    if (!Ready()) return -1;
    auto* entities = plugin.EntitySystem();
    auto* globals = plugin.engine_->GetServerGlobals();
    if (!entities || !globals || globals->maxClients < 0 || globals->maxClients > ABSOLUTE_PLAYER_LIMIT) return -1;
    // At most 64 controllers, once per configured interval; stop on first
    // human. No filesystem, network, schema lookup or player scan per tick.
    for (int slot = 0; slot < globals->maxClients; ++slot) {
        auto* controller = entities->GetEntityInstance(CEntityIndex(slot + 1));
        if (!controller) continue;
        bool human = false;
        if (!slow_animation::ReadHuman(controller, plugin.layout_, FL_FAKECLIENT, human)) return -1;
        if (human) return 1;
    }
    return 0;
}

std::optional<float> Plugin::Backend::Timelimit() const {
    return slow_animation::ReadTimelimit(plugin.timelimit_);
}

void Plugin::Backend::RestoreTimelimit(float limit) {
    if (!slow_animation::WriteTimelimit(plugin.timelimit_, limit)) return;
    plugin.Log("restoring");
}

void Plugin::Backend::Reload(const char* map) {
    if (!Ready() || !slow_animation::ValidMap(map)) return;
    plugin.Log("reloading");
    slow_animation::ReloadMap(map, *plugin.engine_);
}

const char* Plugin::GetLicense() { return "GPLv3"; }
const char* Plugin::GetVersion() { return "1.1.1-api18 @ " GITHUB_SHA; }
const char* Plugin::GetDate() { return __DATE__ " " __TIME__; }
const char* Plugin::GetLogTag() { return "SlowAnimationFix"; }
const char* Plugin::GetAuthor() { return "Slynx; SVAROG fork"; }
const char* Plugin::GetDescription() { return "Empty-server animation clock maintenance"; }
const char* Plugin::GetName() { return "Slow animation fix"; }
const char* Plugin::GetURL() { return "https://github.com/ghostss2016/SlowAnimationFix_mm"; }
