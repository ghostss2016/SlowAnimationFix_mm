#pragma once

#include <ISmmPlugin.h>
#include <eiface.h>
#include <iserver.h>
#include <filesystem.h>
#include <schemasystem/schemasystem.h>
#include <tier1/convar.h>
#include "metamod_virtual_hook.h"
#include "scheduler.h"
#include "owned_convar.h"
#include "sdk/player_layout.h"
#include <atomic>
#include <map>
#include <memory>

#if METAMOD_PLAPI_VERSION < 18
#error SlowAnimationFix requires the official MetaMod plugin API 18 and KHook
#endif

class IUtilsApi;
class CGameEntitySystem;

class Plugin final : public ISmmPlugin, public IMetamodListener {
    struct ConVarApi {
        using Reference = CConVarRef<float>;
        static bool Available();
        static std::unique_ptr<Reference> Create(const char* name);
        static void Register();
        static void Unregister();
    };
    using FrameHook = SvarogHooks::Virtual<ISource2Server, void, bool, bool, bool>;
    using StartupHook = SvarogHooks::Virtual<INetworkServerService, void,
        const GameSessionConfiguration_t&, ISource2WorldSession*, const char*>;
    std::unique_ptr<FrameHook> frameHook_;
    std::unique_ptr<StartupHook> startupHook_;
    slow_animation::OwnedConVarReference<ConVarApi> timelimit_;
    std::atomic<unsigned> callbacks_{0};
    ISource2Server* server_ = nullptr;
    IVEngineServer2* engine_ = nullptr;
    INetworkServerService* network_ = nullptr;
    IFileSystem* filesystem_ = nullptr;
    ISchemaSystem* schema_ = nullptr;
    IUtilsApi* utils_ = nullptr;
    PluginId utilsOwner_ = 0;
    scheduler::Runtime runtime_;
    slow_animation::Settings settings_;
    slow_animation::PlayerLayout layout_;
    std::map<std::string, std::string> messages_;
    bool loaded_ = false;

    bool ReadConfiguration();
    bool ReadFile(const char* path, std::string& output);
    void BindUtils();
    void StartMap(const char* mapName);
    void Log(const char* key);
    const char* Message(const char* key) const;

    struct Backend {
        Plugin& plugin;
        bool Ready() const;
        int HumanCount() const;
        std::optional<float> Timelimit() const;
        void RestoreTimelimit(float limit);
        void Reload(const char* map);
    };

public:
    CGameEntitySystem* EntitySystem() const;
    bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late) override;
    bool Unload(char* error, size_t maxlen) override;
    void AllPluginsLoaded() override;
    void OnPluginLoad(PluginId id) override;
    void OnPluginUnload(PluginId id) override;
    void OnPluginPause(PluginId id) override;
    void OnPluginUnpause(PluginId id) override;
    void OnLevelShutdown() override;
    KHook::Return<void> Hook_GameFrame(ISource2Server*, bool, bool, bool);
    KHook::Return<void> Hook_StartupServer(INetworkServerService*,
        const GameSessionConfiguration_t&, ISource2WorldSession*, const char*);

    const char* GetAuthor() override;
    const char* GetName() override;
    const char* GetDescription() override;
    const char* GetURL() override;
    const char* GetLicense() override;
    const char* GetVersion() override;
    const char* GetDate() override;
    const char* GetLogTag() override;
};

extern Plugin g_Plugin;
PLUGIN_GLOBALVARS();
