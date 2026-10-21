#pragma once

#include <cstdint>
#include <optional>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace nx::app {

// Expanded, ordinary integer indices. Python providers stay at the binding
// boundary; the planner owns all numeric progress without an interpreter.
class ListMovePlanner {
public:
    using Index = std::int64_t;
    ListMovePlanner(std::vector<Index> sources, Index destination);
    std::optional<std::pair<Index, Index>> next();
private:
    std::vector<Index> sources_, popped_;
    Index destination_, increment_ = 0;
    std::size_t position_ = 0;
};

class NestedMoveLedger {
public:
    using Index = ListMovePlanner::Index;
    using Path = std::vector<Index>;
    Path shifted(Path source, std::size_t destination_depth) const;
    Index inserted_before(Index row) const;
    Index removed_before(const Path& parent, Index row) const;
    void commit(const Path& source_parent, Index source_row, Index destination_row);
    const std::vector<std::tuple<Path, Index, Index>>& records() const { return records_; }
private:
    std::map<Path, std::vector<Index>> popped_;
    std::vector<Index> dumped_;
    std::vector<std::tuple<Path, Index, Index>> records_;
};

} // namespace nx::app
