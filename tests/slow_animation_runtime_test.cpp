#include "../src/scheduler.h"
#include "../src/sdk/player_layout.h"
#include "../src/owned_convar.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <vector>

using slow_animation::Settings;
static const Settings settings{1800, 0.1f, "en"};

struct Backend {
    bool ready = true;
    int humans = 0, scans = 0;
    std::optional<float> limit = 60;
    std::vector<std::string> reloads;
    std::vector<float> restored;
    std::function<void()> onReload;
    bool Ready() const { return ready; }
    int HumanCount() { ++scans; return humans; }
    std::optional<float> Timelimit() const { return limit; }
    void RestoreTimelimit(float value) { restored.push_back(value); limit = value; }
    void Reload(const char* map) { reloads.emplace_back(map); if (onReload) onReload(); }
};

// Model the actual SDK contract: construction queues a live reference pointer,
// RegisterAll makes it usable, UnregisterAll invalidates and releases the list.
// This replaces only the SDK boundary, not the production owner/read/write code.
struct ConVarRegistry {
    bool available = true, registrationSucceeds = true, registered = false;
    int created = 0, destroyed = 0, registers = 0, unregisters = 0;
    float value = 60;
    std::vector<std::string> events;
    struct Reference;
    std::vector<Reference*> entries;
    struct Reference {
        ConVarRegistry& registry;
        bool valid = false;
        explicit Reference(ConVarRegistry& state, const char* name) : registry(state) {
            assert(state.available && !state.registered);
            assert(std::string(name) == "mp_timelimit");
            ++state.created;
            state.events.emplace_back("create");
            state.entries.push_back(this);
        }
        ~Reference() {
            assert(!valid);
            assert(std::find(registry.entries.begin(), registry.entries.end(), this) == registry.entries.end());
            ++registry.destroyed;
            registry.events.emplace_back("destroy");
        }
        bool IsValidRef() const { return valid; }
        float Get() const { assert(valid); return registry.value; }
        void Set(float value) { assert(valid); registry.value = value; }
    };
    void Register() {
        assert(available && !registered && entries.size() == 1);
        ++registers;
        events.emplace_back("register");
        registered = true;
        for (auto* entry : entries) entry->valid = registrationSucceeds;
    }
    void Unregister() {
        assert(available && registered && entries.size() == 1);
        ++unregisters;
        events.emplace_back("unregister");
        for (auto* entry : entries) entry->valid = false;
        entries.clear();
        registered = false;
    }
};

struct ConVarApi {
    using Reference = ConVarRegistry::Reference;
    ConVarRegistry* registry = nullptr;
    bool Available() const { return registry && registry->available; }
    std::unique_ptr<Reference> Create(const char* name) {
        return std::make_unique<Reference>(*registry, name);
    }
    void Register() { registry->Register(); }
    void Unregister() { registry->Unregister(); }
};

using OwnedTimelimit = slow_animation::OwnedConVarReference<ConVarApi>;

static void ConVarOwnershipAndRegistration() {
    ConVarRegistry missing;
    missing.available = false;
    OwnedTimelimit unavailable(ConVarApi{&missing});
    assert(!unavailable.Acquire("mp_timelimit"));
    assert(!slow_animation::ReadTimelimit(unavailable));
    assert(!slow_animation::WriteTimelimit(unavailable, 30));
    assert(missing.created == 0 && missing.registers == 0 && missing.unregisters == 0);

    ConVarRegistry rejected;
    rejected.registrationSucceeds = false;
    OwnedTimelimit invalid(ConVarApi{&rejected});
    assert(!invalid.Acquire("mp_timelimit"));
    invalid.Reset(); // cleanup is idempotent after a registration failure
    assert(!slow_animation::ReadTimelimit(invalid));
    assert(rejected.entries.empty() && rejected.destroyed == 1 && rejected.unregisters == 1);
    assert((rejected.events == std::vector<std::string>{"create", "register", "unregister", "destroy"}));

    ConVarRegistry provider;
    OwnedTimelimit timelimit(ConVarApi{&provider});
    {
        auto failedLoad = timelimit.RollbackUnlessCommitted();
        assert(timelimit.Acquire("mp_timelimit"));
        assert(slow_animation::ReadTimelimit(timelimit) == 60);
        // A later hook/setup failure returns without committing, just like Load.
    }
    assert(provider.destroyed == 1 && provider.unregisters == 1 && provider.entries.empty());
    assert(!slow_animation::WriteTimelimit(timelimit, 20));

    // Another module owns a separate registry; this owner's cleanup must not
    // unregister its reference or mutate the engine value through that module.
    ConVarRegistry otherModule;
    OwnedTimelimit other(ConVarApi{&otherModule});
    assert(other.Acquire("mp_timelimit"));
    {
        auto loaded = timelimit.RollbackUnlessCommitted();
        assert(timelimit.Acquire("mp_timelimit"));
        loaded.Commit();
    }
    assert(!timelimit.Acquire("mp_timelimit")); // no repeated registrations
    assert(provider.created == 2 && provider.registers == 2 && provider.unregisters == 1);

    struct RegisteredBackend : Backend {
        OwnedTimelimit& owner;
        explicit RegisteredBackend(OwnedTimelimit& ref) : owner(ref) {}
        std::optional<float> Timelimit() const { return slow_animation::ReadTimelimit(owner); }
        void RestoreTimelimit(float value) {
            assert(slow_animation::WriteTimelimit(owner, value));
            restored.push_back(value);
        }
    } engine(timelimit);
    scheduler::Runtime runtime;
    assert(runtime.Start(settings, 0));
    runtime.MapStart("de_dust2", 0);
    runtime.Frame(1800, engine);
    runtime.MapShutdown();
    runtime.MapStart("de_dust2", 1800);
    runtime.Frame(1800.01, engine);
    assert(provider.value == 30 && engine.restored.size() == 1);
    // Refused/busy unload performs no Reset, so the reference stays usable.
    assert(slow_animation::ReadTimelimit(timelimit) == 30 && provider.unregisters == 1);
    runtime.Shutdown();
    timelimit.Reset(); // successful Unload runs after callbacks/hooks are gone
    timelimit.Reset();
    assert(provider.unregisters == 2 && provider.destroyed == 2 && provider.entries.empty());
    assert(otherModule.registered && otherModule.unregisters == 0 && slow_animation::ReadTimelimit(other) == 60);
    {
        OwnedTimelimit nextLifetime(ConVarApi{&provider});
        assert(nextLifetime.Acquire("mp_timelimit"));
    } // last-resort owner destruction has the same unregister-before-destroy order
    assert(provider.unregisters == 3 && provider.destroyed == 3 && provider.entries.empty());
    for (std::size_t index = 0; index < provider.events.size(); index += 4) {
        assert(provider.events[index] == "create" && provider.events[index + 1] == "register");
        assert(provider.events[index + 2] == "unregister" && provider.events[index + 3] == "destroy");
    }
}

static void ClockAndCadence() {
    scheduler::Runtime runtime;
    Backend engine;
    assert(runtime.Start(settings, 1000000));
    runtime.MapStart("de_dust2", 1000000);
    // Regression: first steady_clock value must not reload immediately.
    runtime.Frame(1000000, engine);
    runtime.Frame(1001799, engine);
    assert(engine.scans == 0 && engine.reloads.empty() && runtime.Now() == 1799);
    assert(runtime.Start(settings, 1001799)); // duplicate listener event
    engine.humans = 1;
    runtime.Frame(1001800, engine);
    runtime.Frame(1001800, engine);
    assert(engine.scans == 1 && engine.reloads.empty());
    // Reversed samples do not increase elapsed time or move the anchor.
    runtime.Frame(999999, engine);
    runtime.Frame(1001801, engine);
    assert(runtime.Now() == 1801);
    engine.humans = -1; // missing entity/schema/globals is not an empty server
    runtime.Frame(1003600, engine);
    assert(engine.scans == 2 && engine.reloads.empty());
    engine.humans = 0;
    runtime.Frame(1010000, engine); // no catch-up scan burst after hibernation
    assert(engine.scans == 3 && engine.reloads.size() == 1);
    runtime.Frame(1010000, engine);
    assert(engine.scans == 3 && engine.reloads.size() == 1);
    runtime.Frame(1011800, engine); // rejected reload can retry at the interval
    assert(engine.scans == 4 && engine.reloads.size() == 2);
}

static void TimelimitAndMapGenerations() {
    scheduler::Runtime runtime;
    Backend engine;
    assert(runtime.Start(settings, 0));
    runtime.MapStart("de_dust2", 0);
    runtime.Frame(1800, engine);
    assert(engine.reloads.size() == 1 && engine.restored.empty());
    runtime.MapShutdown();
    runtime.MapStart("de_dust2", 1800);
    runtime.Frame(1800.01, engine);
    assert(engine.restored.size() == 1 && engine.restored.back() == 30);
    runtime.Frame(1800.02, engine);
    assert(engine.restored.size() == 1);
    runtime.Frame(3600, engine);
    runtime.MapStart("de_dust2", 3600);
    runtime.Frame(3600.01, engine);
    assert(engine.restored.size() == 2 && engine.restored.back() == settings.timelimitFloorMinutes);

    engine.limit = 60;
    runtime.Frame(5400, engine);
    runtime.MapStart("de_nuke", 5400); // don't carry another map's timelimit
    runtime.Frame(5400.01, engine);
    assert(engine.restored.size() == 2);
    runtime.Frame(7200, engine);
    runtime.MapStart("de_nuke", 7200);
    runtime.MapStart("de_inferno", 7200.001); // invalidate queued next-frame restore
    runtime.Frame(7200.01, engine);
    assert(engine.restored.size() == 2);
    engine.limit = 0; // unlimited map: no fabricated timer restoration
    runtime.Frame(9000, engine);
    runtime.MapStart("de_inferno", 9000);
    runtime.Frame(9000.01, engine);
    assert(engine.restored.size() == 2);
}

static void DependencyAndUnload() {
    scheduler::Runtime runtime;
    Backend engine;
    assert(runtime.Start(settings, 0));
    runtime.MapStart("workshop/123/de_custom", 0);
    runtime.Frame(1800, engine);
    runtime.MapStart("workshop/123/de_custom", 1800);
    engine.ready = false;
    runtime.DependencyLost();
    runtime.Frame(1800.01, engine);
    assert(engine.restored.empty());
    runtime.Frame(3600, engine);
    assert(engine.scans == 1);
    engine.ready = true;
    runtime.Frame(5400, engine);
    assert(engine.scans == 2 && engine.reloads.size() == 2);
    runtime.Shutdown();
    runtime.Frame(999999, engine);
    assert(!runtime.Running() && engine.scans == 2 && engine.restored.empty());
    assert(runtime.Start(settings, 1000000)); // next plugin lifetime starts clean
    runtime.MapStart("de_dust2", 1000000);
    runtime.Frame(1000000, engine);
    assert(engine.scans == 2);

    // Engine callbacks can synchronously deliver StartupServer; test the
    // production state machine with re-entrant map lifecycle notifications.
    engine.onReload = [&] { runtime.MapShutdown(); runtime.MapStart("de_dust2", 1001800); };
    runtime.Frame(1001800, engine);
    assert(engine.restored.empty());
    runtime.Frame(1001800.01, engine);
    assert(engine.restored.size() == 1);
}

static void ConfigAndSchemaSafety() {
    Settings parsed;
    assert(slow_animation::ParseSettings("reload_interval_seconds=1800\ntimelimit_floor_minutes=0.1\nlanguage=ru\n", parsed));
    assert(parsed.reloadIntervalSeconds == 1800 && parsed.language == "ru");
    const auto original = parsed;
    for (const auto* invalid : {
        "reload_interval_seconds=0\ntimelimit_floor_minutes=0.1\nlanguage=en\n",
        "reload_interval_seconds=30\ntimelimit_floor_minutes=-1\nlanguage=en\n",
        "reload_interval_seconds=1800x\ntimelimit_floor_minutes=0.1\nlanguage=en\n",
        "reload_interval_seconds=1800\ntimelimit_floor_minutes=nan\nlanguage=en\n",
        "reload_interval_seconds=1800\ntimelimit_floor_minutes=0.1\nlanguage=../ru\n",
        "reload_interval_seconds=1800\ntimelimit_floor_minutes=0.1\nlanguage=en\nlanguage=ru\n"}) {
        assert(!slow_animation::ParseSettings(invalid, parsed));
        assert(parsed.reloadIntervalSeconds == original.reloadIntervalSeconds && parsed.language == original.language);
    }
    assert(slow_animation::ValidMap("de_dust2"));
    assert(slow_animation::ValidMap("workshop/123/de_custom"));
    assert(!slow_animation::ValidMap(nullptr) && !slow_animation::ValidMap(""));
    assert(!slow_animation::ValidMap("de_dust2;quit") && !slow_animation::ValidMap("de_dust2\nquit"));
    assert(!slow_animation::ValidMap(std::string(256, 'a').c_str()));

    std::array<unsigned char, 32> entity{};
    const slow_animation::PlayerLayout layout{0, 5, 13}; // intentionally unaligned
    constexpr std::uint32_t fake = 1u << 8;
    bool human = true;
    assert(slow_animation::ReadHuman(entity.data(), layout, fake, human) && human);
    const std::uint32_t bot = fake;
    std::memcpy(entity.data() + layout.flags, &bot, sizeof(bot));
    assert(slow_animation::ReadHuman(entity.data(), layout, fake, human) && !human);
    entity.fill(0);
    entity[layout.hltv] = 1;
    assert(slow_animation::ReadHuman(entity.data(), layout, fake, human) && !human);
    entity.fill(0);
    entity[layout.connected] = 1;
    assert(slow_animation::ReadHuman(entity.data(), layout, fake, human) && !human);
    assert(!slow_animation::ReadHuman(nullptr, layout, fake, human) && !human);
    assert(!slow_animation::ReadHuman(entity.data(), {}, fake, human) && !human);
    std::uint32_t output = 123;
    assert(!slow_animation::ReadField(entity.data(), -1, output) && output == 123);
    assert(!slow_animation::ReadField(nullptr, 0, output) && output == 123);

    scheduler::Runtime runtime;
    Backend engine;
    assert(runtime.Start(settings, 0));
    runtime.MapStart("de_dust2", 0);
    runtime.Reconfigure(Settings{60, 0.2f, "en"});
    runtime.Frame(59, engine);
    assert(engine.scans == 0);
    runtime.Frame(60, engine);
    assert(engine.scans == 1); // actual settings drive production cadence
}

static void OrdinaryAndWorkshopMaps() {
    struct Engine {
        bool ordinary = true;
        std::vector<std::string> changes, commands;
        bool IsMapValid(const char*) const { return ordinary; }
        void ChangeLevel(const char* map, const char* landmark) { assert(!landmark); changes.emplace_back(map); }
        void ServerCommand(const char* command) { commands.emplace_back(command); }
    } engine;
    assert(slow_animation::ReloadMap("de_dust2", engine));
    assert(engine.changes.size() == 1 && engine.changes[0] == "de_dust2" && engine.commands.empty());
    engine.ordinary = false;
    assert(slow_animation::ReloadMap("workshop/123/de_custom", engine));
    assert(engine.commands.size() == 1 && engine.commands[0] == "ds_workshop_changelevel workshop/123/de_custom\n");
    assert(!slow_animation::ReloadMap("de_dust2;quit", engine));
    assert(engine.changes.size() == 1 && engine.commands.size() == 1);
}

int main() {
    ConVarOwnershipAndRegistration();
    ClockAndCadence();
    TimelimitAndMapGenerations();
    DependencyAndUnload();
    ConfigAndSchemaSafety();
    OrdinaryAndWorkshopMaps();
    std::puts("slow_animation_runtime_test: all checks passed");
}
