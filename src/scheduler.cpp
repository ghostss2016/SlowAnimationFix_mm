#include "scheduler.h"

namespace scheduler {

void Runtime::Advance(double wallNow) {
    if (!std::isfinite(wallNow)) return;
    if (!clockStarted_) {
        lastWall_ = wallNow;
        clockStarted_ = true;
        return;
    }
    // Never turn the first steady_clock sample into server uptime; ignore
    // reversed samples without counting that interval twice on recovery.
    if (wallNow < lastWall_) return;
    now_ += wallNow - lastWall_;
    lastWall_ = wallNow;
}

bool Runtime::Start(const slow_animation::Settings& settings, double wallNow) {
    if (!slow_animation::Valid(settings) || !std::isfinite(wallNow)) return false;
    if (running_) return true; // duplicate AllPluginsLoaded cannot add timers
    Shutdown();
    settings_ = settings;
    running_ = true;
    Advance(wallNow);
    nextCheck_ = settings_.reloadIntervalSeconds;
    return true;
}

void Runtime::Reconfigure(const slow_animation::Settings& settings) {
    if (!slow_animation::Valid(settings)) return;
    const bool intervalChanged = settings_.reloadIntervalSeconds != settings.reloadIntervalSeconds;
    settings_ = settings;
    if (running_ && intervalChanged) nextCheck_ = now_ + settings_.reloadIntervalSeconds;
}

void Runtime::Shutdown() {
    running_ = clockStarted_ = awaitingReload_ = false;
    lastWall_ = now_ = nextCheck_ = mapStart_ = 0;
    ++generation_;
    map_.clear();
    reloadMap_.clear();
    pendingLimit_.reset();
    restoreLimit_.reset();
}

void Runtime::DependencyLost() {
    ++generation_;
    awaitingReload_ = false;
    pendingLimit_.reset();
    restoreLimit_.reset();
    reloadMap_.clear();
}

void Runtime::MapStart(const char* map, double wallNow) {
    Advance(wallNow);
    ++generation_;
    restoreLimit_.reset();
    const std::string next = map ? map : "";
    if (awaitingReload_ && now_ <= nextCheck_ && next == reloadMap_ && pendingLimit_) {
        restoreLimit_ = pendingLimit_;
        restoreGeneration_ = generation_;
    }
    awaitingReload_ = false;
    pendingLimit_.reset();
    reloadMap_.clear();
    map_ = next;
    mapStart_ = now_;
}

void Runtime::MapShutdown() {
    ++generation_;
    map_.clear();
    restoreLimit_.reset();
    // Keep pendingLimit_ only for our expected self-reload StartupServer.
}

} // namespace scheduler
