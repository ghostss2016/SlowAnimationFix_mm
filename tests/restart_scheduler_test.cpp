#include "restart_scheduler.h"
#include <cassert>
#include <string>
#include <vector>

struct Backend {
    std::vector<std::string> commands;
    double now = 0;
    bool ww3Running = false;
    int ww3Checks = 0;

    double Now() const { return now; }
    bool Ww3Running() {
        ++ww3Checks;
        return ww3Running;
    }
    void ChangeMap(const char* name) { commands.emplace_back(name); }
};

int main() {
    restart::Scheduler timer;
    Backend backend;
    timer.MapStart("de_mirage_christmas", 1000);
    backend.now = 4599;
    timer.RoundEnd(backend.now);
    timer.Frame(backend);
    assert(backend.ww3Checks == 0);
    backend.now = 5000;
    timer.Frame(backend);
    assert(backend.commands.empty());
    assert(backend.ww3Checks == 1);
    timer.RoundEnd(backend.now);
    assert(backend.commands.empty());
    timer.Frame(backend);
    timer.RoundEnd(5001);
    timer.Frame(backend);
    assert(backend.commands == std::vector<std::string>{"de_mirage_christmas"});

    timer.MapShutdown();
    timer.MapStart("workshop/123/ww3_map", 5000);
    backend.ww3Running = true;
    backend.now = 8599.99;
    timer.Frame(backend);
    assert(backend.commands.size() == 1);
    assert(backend.ww3Checks == 1);
    backend.now = 8600;
    timer.Frame(backend);
    assert(backend.commands.back() == "workshop/123/ww3_map");
    timer.RoundEnd(8600);
    timer.Frame(backend);
    assert(backend.commands.size() == 2);

    timer.MapStart("de_dust2", 9000);
    backend.ww3Running = false;
    backend.now = 12600;
    timer.Frame(backend);
    assert(backend.commands.size() == 2);
    assert(backend.ww3Checks == 3);
    backend.now += 30;
    timer.Frame(backend);
    assert(backend.commands.size() == 2);
    assert(backend.ww3Checks == 3);
    timer.RoundEnd(backend.now);
    timer.Frame(backend);
    assert(backend.commands.size() == 3);
    assert(backend.commands.back() == "de_dust2");

    timer.MapShutdown();
    timer.MapStart("de_dust2;quit", 9000);
    backend.ww3Running = true;
    timer.RoundEnd(12600);
    timer.Frame(backend);
    assert(backend.commands.size() == 3);

    timer.MapStart("de_nuke", 13000);
    timer.RoundEnd(16600);
    timer.MapShutdown();
    backend.now = 16600;
    timer.Frame(backend);
    assert(backend.commands.size() == 3);
}
