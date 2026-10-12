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
    lastWall_ = now_ = nextCheck_ = 0;
    map_.clear();
}

void Runtime::DependencyLost() {
    awaitingReload_ = false;
}

void Runtime::MapStart(const char* map, double wallNow) {
    Advance(wallNow);
    awaitingReload_ = false;
    map_ = map ? map : "";
}

void Runtime::MapShutdown() {
    map_.clear();
    awaitingReload_ = false;
}

} // namespace scheduler
