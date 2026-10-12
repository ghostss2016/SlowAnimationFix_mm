#pragma once

#include "settings.h"
#include <cmath>
#include <string>

namespace scheduler {

// One plugin-owned periodic empty-server check.
// Engine access is confined to Backend, so the harness calls this production
// scheduler without copying its clock or cancellation algorithm.
class Runtime final {
    slow_animation::Settings settings_{};
    bool running_ = false;
    bool clockStarted_ = false;
    double lastWall_ = 0, now_ = 0, nextCheck_ = 0;
    std::string map_;
    bool awaitingReload_ = false;

    void Advance(double wallNow);

public:
    bool Start(const slow_animation::Settings& settings, double wallNow);
    void Reconfigure(const slow_animation::Settings& settings);
    void Shutdown();
    void DependencyLost();
    void MapStart(const char* map, double wallNow);
    void MapShutdown();
    const std::string& Map() const { return map_; }
    double Now() const { return now_; }
    bool Running() const { return running_; }
    bool AwaitingReload() const { return awaitingReload_; }

    template<class Backend>
    void Frame(double wallNow, Backend& backend) {
        if (!running_) return;
        Advance(wallNow);
        if (now_ < nextCheck_) return;
        // One scan, no catch-up burst after hibernation or a delayed frame.
        nextCheck_ = now_ + settings_.reloadIntervalSeconds;
        // A rejected engine change must not leave an immortal pending task.
        // Expire it at the existing interval, then permit one fresh attempt.
        awaitingReload_ = false;
        if (map_.empty() || !backend.Ready()) return;
        // -1 represents missing schema/globals/entity system. Fail closed.
        if (backend.HumanCount() != 0) return;
        // ChangeLevel loads the server's own mode/map config. This maintenance
        // plugin must not read, decrement, clamp or restore gameplay cvars.
        awaitingReload_ = true;
        const std::string target = map_; // stable if ChangeLevel calls MapStart
        backend.Reload(target.c_str());
    }
};

} // namespace scheduler
