"""Compiler-free migration guards; native behavior is tested centrally."""
from pathlib import Path
import ast
import os
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Api18SourceContract(unittest.TestCase):
    def test_owned_convar_registration_and_load_rollback(self):
        source = (ROOT / "src/plugin.cpp").read_text()
        header = (ROOT / "src/plugin.h").read_text()
        owner = (ROOT / "src/owned_convar.h").read_text()
        self.assertNotIn("static CConVarRef<float>", source)
        self.assertIn("OwnedConVarReference<ConVarApi> timelimit_", header)
        self.assertIn("return g_pCVar != nullptr", source)
        self.assertIn("META_CONVAR_REGISTER(FCVAR_NONE)", source)
        self.assertIn("ConVar_Unregister();", source)
        self.assertIn("slow_animation::ReadTimelimit(plugin.timelimit_)", source)
        self.assertIn("slow_animation::WriteTimelimit(plugin.timelimit_, limit)", source)
        self.assertLess(owner.index("!api_.Available()"), owner.index("api_.Create(name)"))
        self.assertLess(owner.index("api_.Create(name)"), owner.index("api_.Register()"))
        self.assertLess(owner.index("api_.Unregister()"), owner.index("reference_.reset()"))
        load = source.split("bool Plugin::Load(", 1)[1].split("void Plugin::BindUtils()", 1)[0]
        self.assertLess(load.index("g_pCVar, ICvar"), load.index('timelimit_.Acquire("mp_timelimit")'))
        self.assertLess(load.index("timelimit_.RollbackUnlessCommitted()"), load.index('timelimit_.Acquire("mp_timelimit")'))
        self.assertLess(load.index('timelimit_.Acquire("mp_timelimit")'), load.index("frameHook_ ="))
        self.assertLess(load.index("AddListener(this, this)"), load.index("convarLoad.Commit()"))
        unload = source.split("bool Plugin::Unload(", 1)[1].split("bool Plugin::Backend::Ready()", 1)[0]
        self.assertLess(unload.index("callbacks_.load()"), unload.index("timelimit_.Reset()"))
        self.assertLess(unload.index("frameHook_.reset()"), unload.index("timelimit_.Reset()"))
        for language in ("en", "ru"):
            translations = (ROOT / "configs/addons/slow_animation_fix/translations" / (language + ".ini")).read_text()
            self.assertIn("cvar_unavailable =", translations)

    def test_explicit_pinned_khook_include(self):
        ambuild = (ROOT / "AMBuildScript").read_text()
        cmake = (ROOT / "makefiles/shared.cmake").read_text()
        self.assertIn("os.path.join(self.mms_root, 'third_party', 'khook', 'include')", ambuild)
        self.assertIn("${METAMOD_DIR}/third_party/khook/include", cmake)

    def test_real_ambuilder_uses_only_pinned_sdk_protos(self):
        captured = []
        binary = SimpleNamespace(sources=[], custom=[], compiler=SimpleNamespace(
            cxxincludes=[], defines=[], linkflags=[], target=SimpleNamespace(platform="linux")))
        builder = SimpleNamespace(sourcePath=str(ROOT), currentSourcePath=str(ROOT),
            tools=SimpleNamespace(Protoc=lambda **kwargs: captured.append(kwargs)),
            Add=lambda output: SimpleNamespace(binary=output, debug=None))
        sdk = {"name": "cs2", "path": "/pinned/hl2sdk-cs2"}
        plugin = SimpleNamespace(plugin_name="slow_animation_fix", binaries=[],
            sdk_targets=[SimpleNamespace(sdk=sdk, cxx=None, protoc="pinned-protoc")],
            HL2Library=lambda *args: binary)
        source = (ROOT / "AMBuilder").read_text()
        self.assertNotIn("CSGO_PROTO", source)
        exec(compile(ast.parse(source), "AMBuilder", "exec"), {"MMSPlugin": plugin, "builder": builder})
        self.assertEqual(len(captured), 1)
        self.assertEqual(captured[0]["protoc"], "pinned-protoc")
        self.assertEqual(captured[0]["sources"], [
            "/pinned/hl2sdk-cs2/common/network_connection.proto",
            "/pinned/hl2sdk-cs2/common/networkbasetypes.proto",
            "/pinned/hl2sdk-cs2/common/valveextensions.proto"])
        self.assertEqual(plugin.binaries[0].binary, binary)
        cmake = (ROOT / "CMakeLists.txt").read_text()
        proto_recipe = (ROOT / "makefiles/protobuf.cmake").read_text()
        self.assertNotIn("CSGO_PROTO", cmake + proto_recipe)
        self.assertIn('set(SDK_PROTO_DIR "${SOURCESDK_DIR}/common")', cmake)
        for name in ("network_connection", "networkbasetypes", "valveextensions"):
            self.assertIn("${SDK_PROTO_DIR}/" + name + ".proto", proto_recipe)

    def test_official_owned_hooks(self):
        header = (ROOT / "src/plugin.h").read_text()
        source = (ROOT / "src/plugin.cpp").read_text()
        self.assertIn('"metamod_virtual_hook.h"', header)
        self.assertIn("std::unique_ptr<FrameHook>", header)
        self.assertIn("std::unique_ptr<StartupHook>", header)
        self.assertIn("AddInstance(server_)", source)
        self.assertIn("AddInstance(network_)", source)
        self.assertIn("callbacks_.load()", source)
        self.assertIn("startupHook_.reset()", source)
        self.assertIn("frameHook_.reset()", source)
        for forbidden in ("GetApiVersion", "SH_METAMOD_OVERRIDE", "SH_ADD_HOOK", "SH_DECL_HOOK", "sourcehook_metamod_override"):
            self.assertNotIn(forbidden, header + source)

    def test_no_private_library_or_service_offset(self):
        recipes = "\n".join((ROOT / name).read_text() for name in (
            "AMBuildScript", "AMBuilder", "CMakeLists.txt", "makefiles/shared.cmake",
            "makefiles/linux.base.cmake", "makefiles/windows.base.cmake"))
        for forbidden in ("vendor/sourcehook", "vendor/dynlibutils", "AddPublicIncludes", "sourcehook_lib", "dynlibutils_lib"):
            self.assertNotIn(forbidden, recipes)
        sources = "\n".join(path.read_text() for path in (ROOT / "src").rglob("*.*"))
        for forbidden in ("0x50", "0x58", "GameResourceService", "CMemory", "CallVFunc"):
            self.assertNotIn(forbidden, sources)
        self.assertIn("utils_->GetCGameEntitySystem()", sources)
        self.assertIn("utilsOwner_ = 0", sources)
        self.assertIn("runtime_.DependencyLost()", sources)

    def test_schema_checks_and_bounded_tick(self):
        source = (ROOT / "src/plugin.cpp").read_text()
        schema = (ROOT / "src/sdk/player_layout.cpp").read_text()
        self.assertIn("if (!schema)", schema)
        self.assertIn("if (!scope)", schema)
        self.assertIn("!info || !info->m_pFields", schema)
        self.assertIn("offset >= 0", schema)
        self.assertIn("info->m_nSize - width", schema)
        frame = source.split("KHook::Return<void> Plugin::Hook_GameFrame(", 1)[1].split(
            "KHook::Return<void> Plugin::Hook_StartupServer(", 1)[0]
        for forbidden in ("ReadConfiguration(", "GetEntityInstance(", "MetaFactory(", "FindDeclaredClass("):
            self.assertNotIn(forbidden, frame)
        self.assertIn("globals->maxClients > ABSOLUTE_PLAYER_LIMIT", source)

    def test_config_and_installed_package_path(self):
        package = (ROOT / "PackageScript").read_text()
        vdf = (ROOT / "makefiles/metamod/slow_animation_fix.vdf.in").read_text()
        self.assertIn("self.bin_path = self.plugin_folder_path", package)
        self.assertIn("self.addFolder('configs')", package)
        self.assertIn('"addons/slow_animation_fix/slow_animation_fix"', vdf)
        source = (ROOT / "src/plugin.cpp").read_text()
        self.assertIn("slow_animation::ParseSettings(text, next)", source)
        self.assertIn('"addons/slow_animation_fix/slow_animation_fix.ini"', source)
        self.assertIn("runtime_.Reconfigure(settings_)", source)
        settings = (ROOT / "configs/addons/slow_animation_fix/slow_animation_fix.ini").read_text()
        self.assertIn("reload_interval_seconds = 1800", settings)
        self.assertIn("timelimit_floor_minutes = 0.1", settings)

    def test_cloud_workflow_is_retired(self):
        active = ROOT / ".github/workflows"
        self.assertEqual(list(active.glob("*.yml")) + list(active.glob("*.yaml")), [])
        self.assertTrue((ROOT / ".github/disabled-workflows/main.yml.disabled").is_file())

    def test_real_package_producer_matches_central_collector(self):
        class Builder:
            sourcePath = str(ROOT)

            def __init__(self):
                self.folders, self.copies, self.outputs = [], [], {}

            def AddFolder(self, path):
                self.folders.append(path)
                return path

            def AddCopy(self, source, destination):
                self.copies.append((source, destination))

            def AddOutputFile(self, path, content):
                self.outputs[path] = content

        builder = Builder()
        parsed = ast.parse((ROOT / "PackageScript").read_text())
        package_class = next(node for node in parsed.body if isinstance(node, ast.ClassDef) and node.name == "SDKPackage")
        # Execute the existing production class with only the filesystem/build
        # API replaced. No configure step, compiler, subprocess or file write.
        namespace = {"os": os, "builder": builder,
                     "MMSPlugin": SimpleNamespace(plugin_name="slow_animation_fix", plugin_alias="slow_animation_fix")}
        exec(compile(ast.Module(body=[package_class], type_ignores=[]), "PackageScript", "exec"), namespace)
        package = namespace["SDKPackage"](None, "cs2")
        package.addBinary(SimpleNamespace(path="slow_animation_fix.so"))
        self.assertIn("addons/slow_animation_fix", builder.folders)
        self.assertIn("addons/metamod", builder.folders)
        self.assertFalse(any(path.startswith("cs2/") for path in builder.folders))
        destinations = {destination for _, destination in builder.copies}
        for expected in ("addons/slow_animation_fix/slow_animation_fix.so",
                         "addons/slow_animation_fix/slow_animation_fix.ini",
                         "addons/slow_animation_fix/translations/en.ini",
                         "addons/slow_animation_fix/translations/ru.ini",
                         "addons/slow_animation_fix/LICENSE"):
            self.assertIn(expected, destinations)
        self.assertTrue(all(path.startswith("addons/") for path in destinations))
        vdf = builder.outputs["addons/metamod/slow_animation_fix.vdf"]
        self.assertIn(b'"file"\t"addons/slow_animation_fix/slow_animation_fix"', vdf)


if __name__ == "__main__":
    unittest.main()
