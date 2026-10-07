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
interval (1800 seconds in the supplied config), one check visits at most the
SDK's 64 controller slots and stops on the first connected human. Bots and GOTV
are ignored. Missing Utils, entity system, globals or schema prevents a reload.

An empty ordinary map reloads through `IVEngineServer2::ChangeLevel`; a Workshop
map uses `ds_workshop_changelevel` with a validated name. The cadence survives
map changes. It uses elapsed monotonic wall time from the plugin's first sample,
with no first-frame operating-system uptime jump and no catch-up scan burst.
GameFrame performs only clock/deadline checks outside those intervals; it does
not perform filesystem/network access, schema searches or player scans per tick.

For a positive `mp_timelimit`, the remaining minutes are restored on the next
frame after the expected same-map reload. An elapsed limit uses the configured
positive floor (0.1 minutes by default). Unlimited maps remain unlimited. Another
map, another startup generation, dependency unload/pause or plugin unload cancels
the pending restoration. A rejected map change expires at the next normal check,
allowing one fresh attempt without multiplying timers.

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

The package also contains `addons/slow_animation_fix/slow_animation_fix.ini`,
the `translations/en.ini` and `translations/ru.ini` files and the license.
Settings are actually read at load and map start:

```ini
reload_interval_seconds = 1800
timelimit_floor_minutes = 0.1
language = ru
```

A failed configuration/translation reload preserves the last valid state. An
incomplete first installation fails to load rather than supplying hidden mechanic
defaults. Schema lookup errors return an invalid layout; negative offsets/null
objects are never dereferenced. The schema layouts are renewed at map start.

## Build and verification

Request builds through panel `.238` and the central `cs2-ci` runner on `.100`.
The AMBuild recipe uses the pinned SDK/MetaMod, `SchemaEntity` and `cs2-utils`
headers. It accepts `--schema-root` and `--utils-root`, with the standard sibling
workspace defaults. Private SourceHook/DynLibUtils are removed from the actual
build and submodule fetch paths. The former cloud workflow is retained outside
the active workflows directory solely for provenance; do not activate it.

`tests/api18_hook_contract_test.py` is a compiler-free policy guard. The native
`tests/slow_animation_runtime_test.cpp` links the production `src/scheduler.cpp`
and tests clock/cadence, human/unknown snapshots, remaining timelimit, duplicate
events, map generations, dependency loss, unload and config/schema safety. Run
it with ASan/UBSan only through central CI; it does not compile engine hooks or
prove gameplay/native ABI correctness. Native plugin build and a separately
authorized game test remain distinct from source guards or package creation.

## Author and license

Original author: Michal Přikryl (Slynx), [slynxdev.cz](https://slynxdev.cz).
SVAROG fork: [ghostss2016/SlowAnimationFix_mm](https://github.com/ghostss2016/SlowAnimationFix_mm).
GPLv3; see LICENSE.
