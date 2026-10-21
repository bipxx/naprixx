#include "change_journal.hpp"

#include <iostream>
#include <stdexcept>

using nx::app::ChangeJournal;

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        ChangeJournal journal;
        journal.enter();
        journal.mark_primary("point");
        journal.capture("point");
        journal.capture("current_step");
        journal.mark_primary("range");
        journal.capture("range");
        journal.capture("nsteps");
        journal.mark_primary("point");
        journal.capture("point");
        require(journal.depth() == 1 && journal.pending_count() == 4,
                "nested transactions retain the first old value");
        require(journal.comparison_order() == std::vector<std::string>(
                    {"point", "range", "current_step", "nsteps"}),
                "primary changes precede dependent properties");
        require(journal.emissions({true, false, true, false}) ==
                    std::vector<std::string>({"point", "current_step"}),
                "only changed fields and dependencies emit");
        require(journal.emissions({false, false, true, true}).empty(),
                "unchanged primaries suppress dependent-only changes");
        auto shallow = journal;
        auto deep = journal.deep_copy();
        shallow.enter();
        shallow.mark_primary("zoom");
        require(journal.depth() == 1 && shallow.depth() == 2,
                "copying preserves independent delay depths");
        require(journal.primary_names().size() == 3 && deep.primary_names().size() == 2,
                "shallow/deep queue ownership matches model copying");
        journal.leave();
        journal.clear();
        journal.enter();
        journal.mark_primary("callback_change");
        journal.capture("callback_change");
        require(journal.emissions({true}) == std::vector<std::string>({"callback_change"}),
                "callback changes start a fresh transaction after emission selection");
        journal.leave();
        journal.clear();
        require(journal.depth() == 0 && journal.pending_count() == 0,
                "finished transactions release all application change records");
        journal.mark_primary("a");
        journal.mark_primary("b");
        journal.mark_primary("c");
        auto live = journal.iter_primary();
        require(live.next() == "a", "primary iteration starts in insertion order");
        journal.forget_primary("a");
        journal.mark_primary("d");
        require(live.next() == "b" && live.next() == "c",
                "consumed-key deletion preserves pending positions");
        try {
            live.next();
            throw std::logic_error("changed keys must raise");
        } catch (const std::runtime_error& error) {
            require(std::string(error.what()) == "dictionary keys changed during iteration",
                    "same-size key replacement detects excess keys");
        }
        require(!live.next(), "key errors exhaust the iterator");
        auto resized = journal.iter_primary();
        journal.mark_primary("e");
        for (int retry = 0; retry != 2; ++retry) {
            try {
                resized.next();
                throw std::logic_error("changed size must raise");
            } catch (const std::runtime_error& error) {
                require(std::string(error.what()) == "dictionary changed size during iteration",
                        "size errors stay sticky across retries");
            }
        }
        std::cout << "Native change journal contracts passed without Python or Qt\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
