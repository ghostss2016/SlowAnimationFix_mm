#pragma once

#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <string_view>

namespace slow_animation {

struct Settings {
    double reloadIntervalSeconds = 0;
    float timelimitFloorMinutes = 0;
    std::string language;
};

inline bool Valid(const Settings& value) {
    return std::isfinite(value.reloadIntervalSeconds) && value.reloadIntervalSeconds > 0 &&
        std::isfinite(value.timelimitFloorMinutes) && value.timelimitFloorMinutes > 0 &&
        !value.language.empty() && value.language.size() <= 16 &&
        value.language.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_-") == std::string::npos;
}

inline std::string Trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

inline bool ParseEntries(std::string_view text, std::map<std::string, std::string>& output) {
    if (text.size() > 65536) return false;
    std::map<std::string, std::string> next;
    std::istringstream input{std::string(text)};
    std::string line;
    while (std::getline(input, line)) {
        line = Trim(std::move(line));
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const auto equal = line.find('=');
        if (equal == std::string::npos || next.size() >= 64) return false;
        auto key = Trim(line.substr(0, equal));
        auto value = Trim(line.substr(equal + 1));
        if (key.empty() || value.empty() || !next.emplace(std::move(key), std::move(value)).second) return false;
    }
    output = std::move(next);
    return true;
}

inline bool Number(const std::string& text, double& value) {
    std::istringstream input(text);
    input >> value;
    return input && input.eof() && std::isfinite(value);
}

inline bool ParseSettings(std::string_view text, Settings& output) {
    std::map<std::string, std::string> entries;
    if (!ParseEntries(text, entries) || entries.size() != 3 ||
        !entries.count("reload_interval_seconds") || !entries.count("timelimit_floor_minutes") ||
        !entries.count("language")) return false;
    Settings next;
    double floor = 0;
    if (!Number(entries.at("reload_interval_seconds"), next.reloadIntervalSeconds) ||
        !Number(entries.at("timelimit_floor_minutes"), floor)) return false;
    next.timelimitFloorMinutes = static_cast<float>(floor);
    next.language = entries.at("language");
    if (!Valid(next)) return false;
    output = std::move(next);
    return true;
}

// Reject console separators/whitespace before ds_workshop_changelevel.
inline bool ValidMap(const char* name) {
    if (!name || !*name) return false;
    const std::string_view map(name);
    return map.size() < 256 && map.find_first_not_of(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-/.") == std::string_view::npos;
}

// The production engine adapter and the native harness share this branch,
// including safe Workshop command construction and the ordinary-map call.
template<class Engine>
bool ReloadMap(const char* map, Engine& engine) {
    if (!ValidMap(map)) return false;
    if (engine.IsMapValid(map)) {
        engine.ChangeLevel(map, nullptr);
    } else {
        char command[sizeof("ds_workshop_changelevel ") + 256 + 1];
        std::snprintf(command, sizeof(command), "ds_workshop_changelevel %s\n", map);
        engine.ServerCommand(command);
    }
    return true;
}

} // namespace slow_animation
