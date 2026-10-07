#include "../src/scheduler.h"
#include "../src/sdk/player_layout.h"
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
    ClockAndCadence();
    TimelimitAndMapGenerations();
    DependencyAndUnload();
    ConfigAndSchemaSafety();
    OrdinaryAndWorkshopMaps();
    std::puts("slow_animation_runtime_test: all checks passed");
}
