#pragma once

#include "settings.h"
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

namespace scheduler {

// One plugin-owned periodic check and one map-generation-bound restoration.
// Engine access is confined to Backend, so the harness calls this production
// scheduler without copying its clock, timelimit or cancellation algorithm.
class Runtime final {
    slow_animation::Settings settings_{};
    bool running_ = false;
    bool clockStarted_ = false;
    double lastWall_ = 0, now_ = 0, nextCheck_ = 0, mapStart_ = 0;
    std::uint64_t generation_ = 0, restoreGeneration_ = 0;
    std::string map_, reloadMap_;
    bool awaitingReload_ = false;
    std::optional<float> pendingLimit_, restoreLimit_;

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
        // Clear before calling engine code: synchronous map/dependency events
        // must never restore twice or consume a stale next-frame task.
        const auto restore = restoreLimit_;
        restoreLimit_.reset();
        if (restore && restoreGeneration_ == generation_ && backend.Ready())
            backend.RestoreTimelimit(*restore);
        if (now_ < nextCheck_) return;
        // One scan, no catch-up burst after hibernation or a delayed frame.
        nextCheck_ = now_ + settings_.reloadIntervalSeconds;
        // A rejected engine change must not leave an immortal pending task.
        // Expire it at the existing interval, then permit one fresh attempt.
        awaitingReload_ = false;
        pendingLimit_.reset();
        reloadMap_.clear();
        if (map_.empty() || !backend.Ready()) return;
        // -1 represents missing schema/globals/entity system. Fail closed.
        if (backend.HumanCount() != 0) return;
        pendingLimit_.reset();
        const auto limit = backend.Timelimit();
        if (limit && std::isfinite(*limit) && *limit > 0) {
            const double remaining = *limit - (now_ - mapStart_) / 60.0;
            pendingLimit_ = static_cast<float>(
                remaining > settings_.timelimitFloorMinutes
                    ? remaining : settings_.timelimitFloorMinutes);
        }
        reloadMap_ = map_;
        awaitingReload_ = true;
        const std::string target = map_; // stable if ChangeLevel calls MapStart
        backend.Reload(target.c_str());
    }
};

} // namespace scheduler
