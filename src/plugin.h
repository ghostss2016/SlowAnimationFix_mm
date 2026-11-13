#pragma once
#include <ISmmPlugin.h>
#include <engine2/ivengineserver2.h>
#include <engine2/isource2server.h>
#include <engine2/isource2serverconfig.h>
#include <engine2/networkgameserverbase.h>
#include <engine2/inetworkserverservice.h>
#include <igameevents.h>
#include "metamod_virtual_hook.h"
#include "restart_scheduler.h"
#include <atomic>
#include <memory>

#if METAMOD_PLAPI_VERSION < 18
#error SlowAnimationFix requires the official MetaMod plugin API 18 and KHook
#endif

class Plugin final : public ISmmPlugin, public IMetamodListener, public IGameEventListener2 {
    using FrameHook = SvarogHooks::Virtual<ISource2Server, void, bool, bool, bool>;
    using StartupHook = SvarogHooks::Virtual<INetworkServerService, void,
        const GameSessionConfiguration_t&, ISource2WorldSession*, const char*>;
    std::unique_ptr<FrameHook> frameHook_;
    std::unique_ptr<StartupHook> startupHook_;
    std::atomic<unsigned> callbacks_{0};
    ISource2Server* server_ = nullptr;
    IVEngineServer2* engine_ = nullptr;
    INetworkServerService* network_ = nullptr;
    IGameEventManager2* events_ = nullptr;
    restart::Scheduler scheduler_;
    bool listening_ = false;
    bool loaded_ = false;

    void StartMap(const char* mapName);
    void StopListening();
    void StartListening();
public:
    bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late) override;
    bool Unload(char* error, size_t maxlen) override;
    void AllPluginsLoaded() override;
    void OnLevelShutdown() override;
    void FireGameEvent(IGameEvent* event) override;
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
