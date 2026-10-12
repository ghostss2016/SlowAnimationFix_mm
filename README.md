# SlowAnimationFix_mm — SVAROG API18 fork

This fork retains Slynx's empty-server map reload fix and migrates its native
hooks to the official MetaMod plugin API18. It requires the existing SVAROG
Utils provider (`IUtilsApi`). The preserved upstream README and archive notice
are in [docs/provenance/README.upstream-efdf3748.md](docs/provenance/README.upstream-efdf3748.md),
with repository/commit provenance beside it. This fork does not migrate servers
to another plugin or create a second Workshop addon.

## Behavior and lifecycle

At map startup the owned post hook snapshots a validated map name, resolves the
three player schema fields and loads settings/translations. Every configured
interval (1800 seconds in the supplied config), one check visits at most
`ABSOLUTE_PLAYER_LIMIT` controller slots (64 in the pinned SDK) and stops on the first connected human. Bots and GOTV
are ignored. Missing Utils, entity system, globals or schema prevents a reload.

An empty ordinary map reloads through `IVEngineServer2::ChangeLevel`; a Workshop
map uses `ds_workshop_changelevel` with a validated name. The cadence survives
map changes. It uses elapsed monotonic wall time from the plugin's first sample,
with no first-frame operating-system uptime jump and no catch-up scan burst.
GameFrame performs only clock/deadline checks outside those intervals; it does
not perform filesystem/network access, schema searches or player scans per tick.

The plugin does not register, read or write `mp_timelimit` or any other gameplay
cvar. Map/mode settings remain owned by the server configuration and map plugins.
Empty-server maintenance reloads still run at the configured interval. Their
normal ChangeLevel config execution is not overridden by the maintenance plugin.
A rejected map change retries only at the next normal check, without extra timers.

The entity system is borrowed only from `IUtilsApi::GetCGameEntitySystem` during
a check. The provider's MetaMod owner is tracked and invalidated on unload/pause;
no entity-service offsets or retained entity pointers are used. The two typed
KHook registrations are owned and rolled back together on partial failure.
Hot unload is refused while a native callback is active or KHook is unavailable.

## Files and settings

The installed binary and VDF continue to select:

```
addons/slow_animation_fix/slow_animation_fix.so
addons/metamod/slow_animation_fix.vdf
```

AMBuild produces `build/package/addons`, matching the central collector's
`_cibuild/package/addons` contract. It does not add a top-level `cs2/` directory.
The package also contains `addons/slow_animation_fix/slow_animation_fix.ini`,
the `translations/en.ini` and `translations/ru.ini` files and the license.
Settings are actually read at load and map start:

```ini
reload_interval_seconds = 1800
language = ru
```

The old `timelimit_floor_minutes` key is accepted but ignored, so a binary-only
update works with deployed settings without overwriting server configuration.
A failed configuration/translation reload preserves the last valid state. An
incomplete first installation fails to load rather than supplying hidden mechanic
defaults. Schema lookup errors return an invalid layout; negative offsets/null
objects are never dereferenced. The schema layouts are renewed at map start.

## Build and verification

Request builds through panel `.238` and the central `cs2-ci` runner on `.100`.
The AMBuild recipe uses the pinned SDK/MetaMod, `SchemaEntity` and `cs2-utils`
headers. It accepts `--schema-root` and `--utils-root`, with the standard sibling
workspace defaults. Both existing recipes explicitly include the pinned MetaMod
`third_party/khook/include`; the SDK helper supplies only SDK include paths.
Private SourceHook/DynLibUtils are removed from the actual
build and submodule fetch paths. The former cloud workflow is retained outside
the active workflows directory solely for provenance; do not activate it.

`tests/api18_hook_contract_test.py` is a compiler-free policy guard. The native
`tests/slow_animation_runtime_test.cpp` links the production `src/scheduler.cpp`
and tests clock/cadence, human/unknown snapshots, twelve consecutive same-map
reloads without cvar writes, duplicate events, dependency loss, unload and
current/legacy config and schema safety. Its backend has no cvar access methods. Run
it with ASan/UBSan only through central CI; it does not compile engine hooks or
prove gameplay/native ABI correctness. Native plugin build and a separately
authorized game test remain distinct from source guards or package creation.

## Author and license

Original author: Michal Přikryl (Slynx), [slynxdev.cz](https://slynxdev.cz).
SVAROG fork: [ghostss2016/SlowAnimationFix_mm](https://github.com/ghostss2016/SlowAnimationFix_mm).
GPLv3; see LICENSE.
