#include "collection_core.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

using nx::app::ListMovePlanner;
using Index = ListMovePlanner::Index;
using Moves = std::vector<std::pair<Index, Index>>;

void check(std::vector<Index> sources, Index destination, const Moves& expected) {
    ListMovePlanner planner(std::move(sources), destination);
    Moves actual;
    while (auto move = planner.next()) actual.push_back(*move);
    if (actual != expected || planner.next()) throw std::runtime_error("incorrect multi-move plan");
}

int main() {
    try {
        check({}, 0, {});
        check({0}, 0, {});
        check({0, 1, 2}, 0, {{1, 1}, {2, 2}});
        check({4, 2, 4, 0}, 1, {{4, 1}, {3, 2}, {0, 3}});
        check({0, 2, 4}, 6, {{0, 6}, {1, 6}, {2, 6}});
        check({4, 3, 2, 1, 0}, 0, {{4, 0}, {4, 1}, {4, 2}, {4, 3}});
        check({-2, -1}, 0, {{-2, 0}, {-2, 0}});
        bool overflow = false;
        try { ListMovePlanner wide({std::numeric_limits<Index>::max()}, 0); }
        catch (const std::overflow_error&) { overflow = true; }
        if (!overflow) throw std::runtime_error("unsafe numeric plan accepted");
        nx::app::NestedMoveLedger tree;
        if (tree.shifted({2, 3}, 1) != std::vector<Index>({2, 3}))
            throw std::runtime_error("empty nested ledger changes an index");
        tree.commit({2}, 1, 0);
        if (tree.shifted({2, 3}, 1) != std::vector<Index>({2, 4})
            || tree.shifted({2}, 1) != std::vector<Index>({2})
            || tree.removed_before({2}, 3) != 1
            || tree.removed_before({1}, 3) != 0)
            throw std::runtime_error("nested displacement or parent tracking failed");
        tree.commit({2}, 4, 3);
        if (tree.shifted({2, 3}, 1) != std::vector<Index>({2, 5})
            || tree.removed_before({2}, 3) != 1
            || tree.removed_before({2}, 4) != 2)
            throw std::runtime_error("nested cumulative movement failed");
        std::cout << "collection move planning contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
