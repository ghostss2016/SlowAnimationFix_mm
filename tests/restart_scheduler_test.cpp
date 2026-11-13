#include "restart_scheduler.h"
#include <cassert>
#include <string>
#include <vector>

struct Backend {
    std::vector<std::string> commands;
    void ChangeMap(const char* name) { commands.emplace_back(name); }
};

int main() {
    restart::Scheduler timer;
    Backend backend;
    timer.MapStart("de_mirage_christmas", 1000);
    timer.RoundEnd(4599);
    timer.Frame(backend);
    assert(backend.commands.empty());
    timer.RoundEnd(4600);
    assert(backend.commands.empty());
    timer.Frame(backend);
    timer.RoundEnd(4601);
    timer.Frame(backend);
    assert(backend.commands == std::vector<std::string>{"de_mirage_christmas"});
    timer.MapShutdown();
    timer.MapStart("de_mirage_christmas", 5000);
    timer.RoundEnd(8599);
    timer.Frame(backend);
    assert(backend.commands.size() == 1);
    timer.RoundEnd(8600);
    timer.Frame(backend);
    assert(backend.commands.size() == 2);
    timer.MapShutdown();
    timer.MapStart("de_dust2;quit", 9000);
    timer.RoundEnd(12600);
    timer.Frame(backend);
    assert(backend.commands.size() == 2);
}
