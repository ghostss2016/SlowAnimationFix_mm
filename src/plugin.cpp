#include "plugin.h"
#include <IPluginManager.h>
#include <menus.h>
#include <chrono>
#include <cstdio>
#include <cstring>

Plugin g_Plugin;
PLUGIN_EXPOSE(Plugin, g_Plugin);

namespace {
double MonotonicSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool HasRunningWw3() {
    // WW3Enabled001 publishes the plugin identity, without exposing gameplay state.
    // MetaFactory can also find a paused provider, so check the owner's status.
    PluginId owner = 0;
    int result = META_IFACE_FAILED;
    if (!g_SMAPI->MetaFactory("WW3Enabled001", &result, &owner) ||
        result != META_IFACE_OK || owner <= 0) return false;

    auto* manager = static_cast<SourceMM::ISmmPluginManager*>(
        g_SMAPI->MetaFactory(MMIFACE_PLMANAGER, nullptr, nullptr));
    SourceMM::Pl_Status status = SourceMM::Pl_NotFound;
    return manager && manager->Query(owner, nullptr, &status, nullptr) &&
        status == SourceMM::Pl_Running;
}

struct CallbackScope {
    std::atomic<unsigned>& active;
    explicit CallbackScope(std::atomic<unsigned>& count) : active(count) { ++active; }
    ~CallbackScope() { --active; }
};
}

bool Plugin::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool) {
    PLUGIN_SAVEVARS();
    GET_V_IFACE_CURRENT(GetServerFactory, server_, ISource2Server, INTERFACEVERSION_SERVERGAMEDLL);
    GET_V_IFACE_CURRENT(GetEngineFactory, engine_, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, network_, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
    if (!KHook::__exported__khook) {
        g_SMAPI->Format(error, maxlen, "MetaMod KHook unavailable");
        return false;
    }
    frameHook_ = std::make_unique<FrameHook>(&ISource2Server::GameFrame, this, nullptr, &Plugin::Hook_GameFrame);
    startupHook_ = std::make_unique<StartupHook>(&INetworkServerService::StartupServer, this, nullptr, &Plugin::Hook_StartupServer);
    if (!frameHook_->AddInstance(server_) || !startupHook_->AddInstance(network_)) {
        startupHook_.reset();
        frameHook_.reset();
        g_SMAPI->Format(error, maxlen, "SlowAnimationFix hook registration failed");
        return false;
    }
    loaded_ = true;
    g_SMAPI->AddListener(this, this);
    return true;
}

void Plugin::StopListening() {
    if (listening_ && events_) events_->RemoveListener(this);
    listening_ = false;
}

void Plugin::StartListening() {
    if (listening_ || scheduler_.Map().empty()) return;
    // Utils may load after this plugin. Resolve only at lifecycle boundaries,
    // after all plugins loaded or at a map startup, never once per frame.
    if (!events_) {
        int result = META_IFACE_FAILED;
        auto* utils = static_cast<IUtilsApi*>(g_SMAPI->MetaFactory(Utils_INTERFACE, &result, nullptr));
        events_ = utils && result == META_IFACE_OK ? utils->GetGameEventManager() : nullptr;
    }
    if (!events_) {
        META_LOG(this, "Utils game-event manager unavailable; round-end restart inactive");
        return;
    }
    listening_ = events_->AddListener(this, "round_end", true);
    if (!listening_) META_LOG(this, "Cannot subscribe to round_end");
    else META_LOG(this, "Map %s: round_end listener active", scheduler_.Map().c_str());
}

void Plugin::StartMap(const char* mapName) {
    StopListening();
    scheduler_.MapStart(mapName, MonotonicSeconds());
    if (scheduler_.Map().empty()) {
        META_LOG(this, "Invalid current map name; same-map restart disabled");
        return;
    }
    StartListening();
}

void Plugin::AllPluginsLoaded() {
    if (!loaded_ || listening_) return;
    if (scheduler_.Map().empty()) {
        auto* game = network_ ? network_->GetIGameServer() : nullptr;
        if (game && game->GetMapName()) StartMap(game->GetMapName());
    } else {
        StartListening();
    }
}

void Plugin::FireGameEvent(IGameEvent* event) {
    CallbackScope scope(callbacks_);
    if (loaded_ && event && std::strcmp(event->GetName(), "round_end") == 0)
        scheduler_.RoundEnd(MonotonicSeconds());
}

KHook::Return<void> Plugin::Hook_GameFrame(ISource2Server*, bool, bool, bool) {
    CallbackScope scope(callbacks_);
    if (loaded_ && engine_) {
        struct Backend {
            IVEngineServer2& engine;
            double Now() const { return MonotonicSeconds(); }
            bool Ww3Running() const { return HasRunningWw3(); }
            void ChangeMap(const char* map) {
                char command[sizeof("mm_map ") + 256 + 1];
                std::snprintf(command, sizeof(command), "mm_map %s\n", map);
                engine.ServerCommand(command);
            }
        } backend{*engine_};
        scheduler_.Frame(backend);
    }
    return {KHook::Action::Ignore};
}

KHook::Return<void> Plugin::Hook_StartupServer(INetworkServerService*,
    const GameSessionConfiguration_t&, ISource2WorldSession*, const char* mapName) {
    CallbackScope scope(callbacks_);
    StartMap(mapName);
    return {KHook::Action::Ignore};
}

void Plugin::OnLevelShutdown() {
    StopListening();
    scheduler_.MapShutdown();
}

bool Plugin::Unload(char* error, size_t maxlen) {
    if (callbacks_.load() || ((frameHook_ || startupHook_) && !KHook::__exported__khook)) {
        g_SMAPI->Format(error, maxlen, "SlowAnimationFix callback active or KHook unavailable");
        return false;
    }
    loaded_ = false;
    StopListening();
    scheduler_.MapShutdown();
    startupHook_.reset();
    frameHook_.reset();
    return true;
}

const char* Plugin::GetLicense() { return "GPLv3"; }
const char* Plugin::GetVersion() { return "2.1.0 @ " GITHUB_SHA; }
const char* Plugin::GetDate() { return __DATE__ " " __TIME__; }
const char* Plugin::GetLogTag() { return "SlowAnimationFix"; }
const char* Plugin::GetAuthor() { return "Slynx; SVAROG fork"; }
const char* Plugin::GetDescription() { return "Restart the same map after 60 minutes, without waiting for round end in WW3"; }
const char* Plugin::GetName() { return "Slow animation fix"; }
const char* Plugin::GetURL() { return "https://github.com/ghostss2016/SlowAnimationFix_mm"; }
