"""Central-only native regression for actual bridge/lifecycle/backend bodies.

Run via the existing .238 -> cs2-ci route, never a local production compiler.
Only engine/MetaMod boundaries are doubled; Runtime and player-field readers are
the production code. A built module path can additionally verify ELF closure.
"""
from pathlib import Path
import ast
import os
import shlex
import subprocess
import tempfile
import unittest
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]


def production_function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[start:end]


def packaged_resources():
    """Run the actual package class; replace only its build/copy boundary."""
    class Builder:
        sourcePath = str(ROOT)

        def __init__(self):
            self.files = {}

        def AddFolder(self, path):
            return path

        def AddCopy(self, source, destination):
            if destination in self.files:
                raise AssertionError("duplicate package destination: " + destination)
            self.files[destination] = Path(source).read_bytes()

        def AddOutputFile(self, path, content):
            if path in self.files:
                raise AssertionError("duplicate package destination: " + path)
            self.files[path] = content

    builder = Builder()
    parsed = ast.parse((ROOT / "PackageScript").read_text())
    package_class = next(node for node in parsed.body if isinstance(node, ast.ClassDef) and node.name == "SDKPackage")
    namespace = {"os": os, "builder": builder,
                 "MMSPlugin": SimpleNamespace(plugin_name="slow_animation_fix", plugin_alias="slow_animation_fix")}
    exec(compile(ast.Module(body=[package_class], type_ignores=[]), "PackageScript", "exec"), namespace)
    namespace["SDKPackage"](None, "cs2")
    return builder.files


def resource_harness():
    source = (ROOT / "src/plugin.cpp").read_text()
    bodies = "\n".join(production_function(source, signature) for signature in (
        "bool Plugin::ReadFile(const char* path, std::string& output)",
        "bool Plugin::ReadConfiguration()",
        "const char* Plugin::Message(const char* key) const",
    ))
    fixtures = []
    for path, content in sorted(packaged_resources().items()):
        if not path.endswith(".ini"):
            continue
        text = content.decode("utf-8")
        assert ')SVARGINI"' not in text
        fixtures.append('    fs.files.emplace("' + path + '", R"SVARGINI(' + text + ')SVARGINI");')
    return r'''
#include "scheduler.h"
#include <cassert>
#include <cstring>
#include <map>
#include <string>

constexpr int FILESYSTEM_INVALID_HANDLE = -1;
struct FileSystem {
    std::map<std::string, std::string> files;
    const std::string* current = nullptr;
    int closes = 0, opens = 0;
    bool shortRead = false;
    int Open(const char* path, const char* mode, const char* search) {
        assert(std::strcmp(mode, "r") == 0 && std::strcmp(search, "GAME") == 0);
        assert(current == nullptr); ++opens;
        const auto found = files.find(path);
        if (found == files.end()) return FILESYSTEM_INVALID_HANDLE;
        current = &found->second; return 1;
    }
    unsigned int Size(int handle) { assert(handle == 1 && current); return current->size(); }
    int Read(void* output, int length, int handle) {
        assert(handle == 1 && current && length == int(current->size()));
        const int count = length - int(shortRead);
        std::memcpy(output, current->data(), count); return count;
    }
    void Close(int handle) { assert(handle == 1 && current); current = nullptr; ++closes; }
};
class Plugin {
public:
    FileSystem* filesystem_ = nullptr;
    slow_animation::Settings settings_;
    std::map<std::string, std::string> messages_;
    scheduler::Runtime runtime_;
    bool ReadFile(const char*, std::string&);
    bool ReadConfiguration();
    const char* Message(const char*) const;
};
''' + bodies + r'''

int main() {
    FileSystem fs;
''' + "\n".join(fixtures) + r'''
    const std::string configPath = "addons/slow_animation_fix/slow_animation_fix.ini";
    slow_animation::Settings packaged;
    assert(slow_animation::ParseSettings(fs.files.at(configPath), packaged));
    const std::string languagePath = "addons/slow_animation_fix/translations/" + packaged.language + ".ini";
    assert(fs.files.count(languagePath)); // selected language is in the actual package
    const auto originalFiles = fs.files;
    Plugin plugin;
    assert(!plugin.ReadConfiguration() && plugin.messages_.empty());
    plugin.filesystem_ = &fs;
    fs.files.erase(configPath);
    assert(!plugin.ReadConfiguration() && plugin.messages_.empty() && plugin.settings_.language.empty());
    fs.files = originalFiles;
    fs.files.erase(languagePath);
    assert(!plugin.ReadConfiguration() && plugin.messages_.empty() && plugin.settings_.language.empty());
    fs.files = originalFiles;
    assert(plugin.ReadConfiguration());
    assert(plugin.settings_.language == packaged.language);
    assert(plugin.settings_.reloadIntervalSeconds == packaged.reloadIntervalSeconds);
    assert(plugin.settings_.timelimitFloorMinutes == packaged.timelimitFloorMinutes);
    std::map<std::string, std::string> expectedMessages;
    assert(slow_animation::ParseEntries(fs.files.at(languagePath), expectedMessages));
    assert(plugin.messages_ == expectedMessages);
    for (const auto& [key, value] : expectedMessages) {
        assert(std::string(plugin.Message(key.c_str())) == value);
    }
    assert(std::string(plugin.Message("unknown_key")) == "unknown_key");
    // Missing keys reject the whole reload without changing valid settings or texts.
    for (const auto& [missing, value] : expectedMessages) {
        std::string incomplete;
        for (const auto& [key, text] : expectedMessages) {
            if (key != missing) incomplete += key + "=" + text + "\n";
        }
        fs.files[languagePath] = incomplete;
        assert(!plugin.ReadConfiguration());
        assert(plugin.messages_ == expectedMessages && plugin.settings_.language == packaged.language);
        assert(plugin.settings_.reloadIntervalSeconds == packaged.reloadIntervalSeconds);
        assert(fs.current == nullptr);
    }
    fs.files = originalFiles;
    for (const auto& malformed : {std::string{}, std::string(65537, 'x'),
                                 std::string("reload_interval_seconds=0\ntimelimit_floor_minutes=0.1\nlanguage=ru\n"),
                                 std::string("reload_interval_seconds=5\ntimelimit_floor_minutes=0.1\nlanguage=../ru\n")}) {
        fs.files[configPath] = malformed;
        assert(!plugin.ReadConfiguration() && plugin.messages_ == expectedMessages);
        assert(plugin.settings_.reloadIntervalSeconds == packaged.reloadIntervalSeconds && fs.current == nullptr);
    }
    fs.files = originalFiles;
    fs.shortRead = true;
    assert(!plugin.ReadConfiguration() && fs.current == nullptr);
    fs.shortRead = false;
    // Runtime must consume changed config values; no mechanics defaults in code.
    assert(plugin.runtime_.Start(plugin.settings_, 0));
    plugin.runtime_.MapStart("de_dust2", 0);
    fs.files[configPath] = "reload_interval_seconds=5\ntimelimit_floor_minutes=0.7\nlanguage=en\n";
    assert(plugin.ReadConfiguration());
    assert(plugin.settings_.reloadIntervalSeconds == 5 && plugin.settings_.timelimitFloorMinutes == 0.7f);
    std::map<std::string, std::string> english;
    assert(slow_animation::ParseEntries(fs.files.at("addons/slow_animation_fix/translations/en.ini"), english));
    assert(plugin.messages_ == english && plugin.settings_.language == "en");
    struct Backend {
        int scans = 0;
        bool Ready() const { return true; }
        int HumanCount() { ++scans; return 1; }
        std::optional<float> Timelimit() const { assert(false); return {}; }
        void RestoreTimelimit(float) { assert(false); }
        void Reload(const char*) { assert(false); }
    } backend;
    plugin.runtime_.Frame(4, backend); assert(backend.scans == 0);
    plugin.runtime_.Frame(5, backend); assert(backend.scans == 1);
    assert(fs.current == nullptr && fs.closes > 0);
}
'''


def harness():
    source = (ROOT / "src/plugin.cpp").read_text()
    bodies = "\n".join(production_function(source, signature) for signature in (
        "CGameEntitySystem* GameEntitySystem()",
        "void Plugin::BindUtils()",
        "CGameEntitySystem* Plugin::EntitySystem() const",
        "void Plugin::OnPluginLoad(PluginId)",
        "void Plugin::OnPluginUnpause(PluginId)",
        "void Plugin::OnPluginPause(PluginId id)",
        "void Plugin::OnPluginUnload(PluginId id)",
        "void Plugin::OnLevelShutdown()",
        "bool Plugin::Backend::Ready() const",
        "int Plugin::Backend::HumanCount() const",
    ))
    return r'''
#include "scheduler.h"
#include "sdk/player_layout.h"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#define META_NO_HL2SDK
#include <ISmmAPI.h>

static_assert(METAMOD_PLAPI_VERSION >= 18);
using MetaFactorySignature = void* (SourceMM::ISmmAPI::*)(const char*, int*, PluginId*);
static_assert(std::is_same_v<decltype(&SourceMM::ISmmAPI::MetaFactory), MetaFactorySignature>);

constexpr int META_IFACE_OK = 0;
constexpr const char* Utils_INTERFACE = "IUtilsApi";
constexpr int ABSOLUTE_PLAYER_LIMIT = 64;
constexpr std::uint32_t FL_FAKECLIENT = 1U << 8;
struct CEntityIndex { int value; explicit CEntityIndex(int index) : value(index) {} };
struct Controller { std::uint32_t connected = 0, flags = 0; bool hltv = false; };
struct CGameEntitySystem {
    int reads = 0;
    std::array<Controller*, 65> controllers{};
    Controller* GetEntityInstance(CEntityIndex index) {
        assert(index.value > 0 && index.value <= 64); ++reads;
        return controllers[index.value];
    }
};
struct Globals { int maxClients = 64; };
struct Engine {
    Globals globals;
    bool available = true;
    Globals* GetServerGlobals() { return available ? &globals : nullptr; }
};
struct IUtilsApi {
    CGameEntitySystem* entities = nullptr;
    int calls = 0;
    CGameEntitySystem* GetCGameEntitySystem() { ++calls; return entities; }
};
// Use the actual pinned manager vtable and statuses, doubling only its calls.
struct PluginManagerDouble final : SourceMM::ISmmPluginManager {
    PluginId owner = 20;
    SourceMM::Pl_Status status = SourceMM::Pl_Running;
    bool found = true;
    int queries = 0;
    PluginId Load(const char*, PluginId, bool&, char*, size_t) override { assert(false); return 0; }
    bool Unload(PluginId, bool, char*, size_t) override { assert(false); return false; }
    bool Pause(PluginId, char*, size_t) override { assert(false); return false; }
    bool Unpause(PluginId, char*, size_t) override { assert(false); return false; }
    bool UnloadAll() override { assert(false); return false; }
    bool QueryRunning(PluginId, char*, size_t) override { assert(false); return false; }
    bool QueryHandle(PluginId, void**) override { assert(false); return false; }
    bool Query(PluginId id, const char**, SourceMM::Pl_Status* output, PluginId*) override {
        ++queries;
        if (!found || id != owner) return false;
        *output = status; return true;
    }
};
struct Factory {
    SourceMM::ISmmPluginManager* manager = nullptr;
    IUtilsApi* utils = nullptr;
    PluginId owner = 20;
    int result = META_IFACE_OK;
    void* MetaFactory(const char* name, int* code, PluginId* plugin) {
        if (std::strcmp(name, MMIFACE_PLMANAGER) == 0) return static_cast<void*>(manager);
        assert(std::strcmp(name, Utils_INTERFACE) == 0);
        if (code) *code = result;
        if (plugin) *plugin = owner;
        return utils;
    }
};
Factory* g_SMAPI = nullptr;
class Plugin {
public:
    bool loaded_ = false;
    IUtilsApi* utils_ = nullptr;
    PluginId utilsOwner_ = 0;
    Engine* engine_ = nullptr;
    slow_animation::PlayerLayout layout_;
    scheduler::Runtime runtime_;
    void BindUtils();
    CGameEntitySystem* EntitySystem() const;
    void OnPluginLoad(PluginId);
    void OnPluginUnpause(PluginId);
    void OnPluginPause(PluginId);
    void OnPluginUnload(PluginId);
    void OnLevelShutdown();
    struct Backend {
        Plugin& plugin;
        bool Ready() const;
        int HumanCount() const;
    };
};
Plugin g_Plugin;
''' + bodies + r'''

int main() {
    CGameEntitySystem first, second;
    IUtilsApi utils; utils.entities = &first;
    IUtilsApi replacement; replacement.entities = &second;
    PluginManagerDouble manager;
    Factory factory; factory.manager = &manager; factory.utils = &utils;
    g_SMAPI = &factory;
    Engine engine;
    g_Plugin.engine_ = &engine;
    g_Plugin.layout_ = {offsetof(Controller, connected), offsetof(Controller, flags), offsetof(Controller, hltv)};
    Plugin::Backend backend{g_Plugin};
    assert(GameEntitySystem() == nullptr && utils.calls == 0);
    g_Plugin.OnPluginLoad(20); // not loaded: cannot bind yet
    assert(g_Plugin.utils_ == nullptr);
    g_Plugin.loaded_ = true;
    assert(GameEntitySystem() == nullptr);
    g_Plugin.OnPluginLoad(20);
    assert(GameEntitySystem() == &first && utils.calls == 1);
    // Repeated load/unpause notifications retain one binding without touching
    // the provider; there are no provider callbacks or retained entity pointers.
    const int afterBind = utils.calls;
    g_Plugin.OnPluginLoad(20);
    g_Plugin.OnPluginUnpause(20);
    assert(utils.calls == afterBind);
    assert(backend.HumanCount() == 0 && first.reads == 64);
    Controller human;
    first.controllers[1] = &human; first.reads = 0;
    assert(backend.HumanCount() == 1 && first.reads == 1); // early exit
    human.flags = FL_FAKECLIENT;
    assert(backend.HumanCount() == 0);
    human.flags = 0; human.hltv = true;
    assert(backend.HumanCount() == 0);
    human.hltv = false; human.connected = 1;
    assert(backend.HumanCount() == 0);
    engine.globals.maxClients = 65;
    assert(backend.HumanCount() == -1);
    engine.globals.maxClients = -1;
    assert(backend.HumanCount() == -1);
    engine.globals.maxClients = 0;
    const int noClientsReads = first.reads;
    assert(backend.HumanCount() == 0 && first.reads == noClientsReads);
    engine.globals.maxClients = 64;
    engine.available = false; assert(backend.HumanCount() == -1); engine.available = true;
    const int beforeInvalid = utils.calls;
    for (auto status : {SourceMM::Pl_NotFound, SourceMM::Pl_Paused, SourceMM::Pl_Error, SourceMM::Pl_Refused}) {
        manager.status = status;
        assert(GameEntitySystem() == nullptr && backend.HumanCount() == -1);
    }
    manager.status = SourceMM::Pl_Running;
    manager.found = false; assert(GameEntitySystem() == nullptr); manager.found = true;
    factory.manager = nullptr; assert(GameEntitySystem() == nullptr); factory.manager = &manager;
    assert(utils.calls == beforeInvalid); // no call into any inactive owner
    factory.owner = 21; assert(GameEntitySystem() == nullptr); factory.owner = 20;
    factory.result = 1; assert(GameEntitySystem() == nullptr); factory.result = META_IFACE_OK;
    factory.utils = &replacement; assert(GameEntitySystem() == nullptr); factory.utils = &utils;
    g_SMAPI = nullptr; assert(GameEntitySystem() == nullptr); g_SMAPI = &factory;
    assert(utils.calls == beforeInvalid);
    utils.entities = nullptr; assert(GameEntitySystem() == nullptr); utils.entities = &first;
    g_Plugin.OnPluginUnload(999); assert(GameEntitySystem() == &first);

    // Actual scheduler -> actual HumanCount verifies the hot path and bounds:
    // no manager/provider/scan work occurs between configured checks, even
    // after a long pause, and a human prevents a real scheduled reload.
    assert(g_Plugin.runtime_.Start({1800, 0.1f, "en"}, 0));
    g_Plugin.runtime_.MapStart("de_dust2", 0);
    struct RuntimeBackend : Plugin::Backend {
        int reloads = 0;
        explicit RuntimeBackend(Plugin& plugin) : Plugin::Backend{plugin} {}
        std::optional<float> Timelimit() const { return 60; }
        void RestoreTimelimit(float) { assert(false); }
        void Reload(const char*) { ++reloads; }
    } scheduled(g_Plugin);
    const int idleQueries = manager.queries, idleProviderCalls = utils.calls, idleReads = first.reads;
    for (int frame = 0; frame < 1800; ++frame) g_Plugin.runtime_.Frame(frame, scheduled);
    assert(manager.queries == idleQueries && utils.calls == idleProviderCalls && first.reads == idleReads);
    human.connected = 0;
    g_Plugin.runtime_.Frame(1800, scheduled);
    assert(manager.queries == idleQueries + 1 && utils.calls == idleProviderCalls + 1);
    assert(first.reads == idleReads + 1 && scheduled.reloads == 0);
    first.controllers[1] = nullptr;
    g_Plugin.runtime_.Frame(10000, scheduled);
    assert(manager.queries == idleQueries + 2 && utils.calls == idleProviderCalls + 2);
    assert(first.reads == idleReads + 65 && scheduled.reloads == 1);
    g_Plugin.runtime_.Frame(10000, scheduled);
    assert(manager.queries == idleQueries + 2 && scheduled.reloads == 1);
    g_Plugin.runtime_.Shutdown();

    // Exercise actual Runtime cancellation through actual dependency events.
    const slow_animation::Settings settings{1800, 0.1f, "en"};
    assert(g_Plugin.runtime_.Start(settings, 0));
    g_Plugin.runtime_.MapStart("de_dust2", 0);
    struct EmptyEngine {
        bool Ready() const { return true; }
        int HumanCount() const { return 0; }
        std::optional<float> Timelimit() const { return 60; }
        void RestoreTimelimit(float) { assert(false); }
        void Reload(const char*) {}
    } empty;
    g_Plugin.runtime_.Frame(1800, empty);
    assert(g_Plugin.runtime_.AwaitingReload());
    g_Plugin.OnPluginPause(20);
    assert(!g_Plugin.runtime_.AwaitingReload());
    assert(g_Plugin.utilsOwner_ == 0 && GameEntitySystem() == nullptr);
    factory.utils = &replacement; factory.owner = 21; manager.owner = 21;
    g_Plugin.OnPluginUnpause(21);
    assert(GameEntitySystem() == &second); // no pointer retained from previous provider/map
    g_Plugin.OnLevelShutdown();
    assert(g_Plugin.runtime_.Map().empty() && !g_Plugin.layout_.Ready());
    assert(!backend.Ready() && backend.HumanCount() == -1);
    g_Plugin.OnPluginUnload(21);
    assert(GameEntitySystem() == nullptr);
    g_Plugin.loaded_ = false;
    g_Plugin.OnPluginLoad(21); assert(GameEntitySystem() == nullptr);
}
'''


class EntityBridge(unittest.TestCase):
    def compile_and_run(self, content, name):
        # MMS is exported by the canonical build-fleet runner. Its sibling path
        # is the same pinned workspace layout, never an arbitrary system SDK.
        metamod = Path(os.environ.get("MMS", str(ROOT.parent / "metamod-source")))
        self.assertTrue((metamod / "core/ISmmAPI.h").is_file(), "pinned MetaMod headers are required")
        with tempfile.TemporaryDirectory(prefix="slow-animation-" + name + "-") as directory:
            directory = Path(directory)
            source, executable = directory / (name + ".cpp"), directory / name
            source.write_text(content)
            command = shlex.split(os.environ.get("CXX", "clang++")) + [
                "-std=c++20", "-O1", "-g", "-UNDEBUG", "-fno-omit-frame-pointer",
                "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-I" + str(ROOT / "src"), "-I" + str(metamod / "core"),
                str(source), str(ROOT / "src/scheduler.cpp"), "-o", str(executable)]
            subprocess.run(command, check=True, timeout=60)
            subprocess.run([str(executable)], check=True, timeout=20)

    def test_actual_module_owned_bridge_and_lifecycle(self):
        self.compile_and_run(harness(), "bridge")

    def test_actual_package_resources_and_configuration_lifecycle(self):
        self.compile_and_run(resource_harness(), "resources")

    @unittest.skipUnless(os.environ.get("SLOW_ANIMATION_MODULE_IMAGE"), "built SO closure is checked after the central build")
    def test_built_module_has_no_undefined_entity_accessor(self):
        image = Path(os.environ["SLOW_ANIMATION_MODULE_IMAGE"])
        self.assertTrue(image.is_file())
        symbols = subprocess.check_output(["readelf", "--dyn-syms", "--wide", str(image)], text=True)
        unresolved = [line for line in symbols.splitlines() if " UND " in line and "_Z16GameEntitySystemv" in line]
        self.assertEqual(unresolved, [], "SDK accessor must be resolved inside this SO, not by another plugin")


if __name__ == "__main__":
    unittest.main()
