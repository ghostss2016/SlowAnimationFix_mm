# SlowAnimationFix_mm — SVAROG fork

This plugin restarts the current CS2 map after it has run for 60 minutes. It waits for the active round's `round_end` event, then submits `mm_map <current map>` from the server console on the next game frame. For example, on `de_mirage_christmas`, it submits `mm_map de_mirage_christmas`.

The 60-minute clock starts at map startup and resets when the map changes. A single command is sent per map lifetime. The plugin never interrupts a round. If a mode does not emit `round_end`, it leaves the map running. The map name is validated before it is placed in a console command.

The previous empty-server player scan, Schema/entity-system dependency, direct ChangeLevel/Workshop reload path, configuration and translations are no longer part of the build or package. The existing Utils API is used only to obtain the engine-owned game-event manager; the plugin does not retain a Utils API pointer. No gameplay cvars are read or changed.

The installed binary and VDF paths remain `addons/slow_animation_fix/slow_animation_fix.so` and `addons/metamod/slow_animation_fix.vdf`.

Builds and delivery use the existing panel `.238` and central `cs2-ci` on `.100`. The native runtime behavior still requires verification on a game server after the central build.

Original author: Michal Přikryl (Slynx). SVAROG fork: https://github.com/ghostss2016/SlowAnimationFix_mm. License: GPLv3.
