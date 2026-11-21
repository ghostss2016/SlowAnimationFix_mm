# SlowAnimationFix_mm — SVAROG fork

This plugin restarts the current CS2 map with `mm_map <current map>`. On a server running WW3, it submits the command on the first game frame at or after 60 minutes of map time. Other modes wait for the first `round_end` at or after 60 minutes and submit the command on the next game frame.

The 60-minute clock starts at map startup and resets when the map changes. A single command is sent per map lifetime. The map name is validated before it is placed in a console command. At the deadline, the plugin queries the `WW3Enabled001` Metamod API once. The API identifies WW3, and the Metamod plugin manager confirms its running status. The WW3 path requires a WW3 build that provides this API.

The previous empty-server player scan, Schema/entity-system dependency, direct ChangeLevel/Workshop reload path, configuration and translations are no longer part of the build or package. The existing Utils API is used only to obtain the engine-owned game-event manager; the plugin does not retain a Utils API pointer. Event-manager lookup waits for the all-plugins-loaded/map-start lifecycle, so Utils may appear later in the load order. No gameplay cvars are read or changed.

The installed binary and VDF paths remain `addons/slow_animation_fix/slow_animation_fix.so` and `addons/metamod/slow_animation_fix.vdf`.

Builds and delivery use the existing panel `.238` and central `cs2-ci` on `.100`. The native runtime behavior still requires verification on a game server after the central build.

Original author: Michal Přikryl (Slynx). SVAROG fork: https://github.com/ghostss2016/SlowAnimationFix_mm. License: GPLv3.
