#pragma once
#include <cmath>
#include <string>
#include <string_view>

namespace restart {
inline bool ValidMap(const char* name) {
    if (!name || !*name) return false;
    const std::string_view value(name);
    return value.size() < 256 && value.find_first_not_of(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-/.") == std::string_view::npos;
}

class Scheduler final {
    static constexpr double Interval = 3600.0;
    std::string map_;
    double started_ = 0;
    bool pending_ = false;
    bool sent_ = false;
public:
    void MapStart(const char* map, double now) {
        map_ = ValidMap(map) ? map : "";
        started_ = now;
        pending_ = sent_ = false;
    }
    void MapShutdown() { map_.clear(); pending_ = sent_ = false; }
    void RoundEnd(double now) {
        if (map_.empty() || sent_ || !std::isfinite(now) || now < started_) return;
        if (now - started_ >= Interval) pending_ = true;
    }
    template<class Backend> void Frame(Backend& backend) {
        if (!pending_ || sent_ || map_.empty()) return;
        pending_ = false;
        sent_ = true;
        backend.ChangeMap(map_.c_str());
    }
    const std::string& Map() const { return map_; }
};
}
