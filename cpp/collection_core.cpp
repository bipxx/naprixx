#include "collection_core.hpp"
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace nx::app {

ListMovePlanner::ListMovePlanner(std::vector<Index> sources, Index destination)
    : destination_(destination) {
    const auto maximum = std::numeric_limits<Index>::max();
    if (sources.size() > static_cast<std::size_t>(maximum / 4))
        throw std::overflow_error("move plan is too large");
    const auto margin = static_cast<Index>(sources.size()) * 2 + 2;
    const auto safe = [maximum, margin](Index value) {
        return value >= -maximum + margin && value <= maximum - margin;
    };
    if (!safe(destination)) throw std::overflow_error("move destination requires a wider index");
    std::unordered_set<Index> seen;
    for (Index source : sources) {
        if (!safe(source)) throw std::overflow_error("move source requires a wider index");
        if (seen.insert(source).second) sources_.push_back(source);
    }
    popped_.reserve(sources_.size());
}

std::optional<std::pair<ListMovePlanner::Index, ListMovePlanner::Index>> ListMovePlanner::next() {
    while (position_ < sources_.size()) {
        const auto position = static_cast<Index>(position_++);
        Index source = sources_[static_cast<std::size_t>(position)];
        const bool move = source != destination_;
        if (move) {
            Index removed = 0;
            for (Index previous : popped_) if (previous <= source) ++removed;
            source -= removed;
            if (source >= destination_) source += position;
        }
        const auto result = std::make_pair(source, destination_ + increment_);
        popped_.push_back(source);
        if (destination_ <= source) ++increment_;
        if (move) return result;
    }
    return std::nullopt;
}

NestedMoveLedger::Path NestedMoveLedger::shifted(Path source, std::size_t destination_depth) const {
    if (source.size() > destination_depth) {
        Index count = inserted_before(source[destination_depth]);
        if (source[destination_depth] > std::numeric_limits<Index>::max() - count)
            throw std::overflow_error("nested move source requires a wider index");
        source[destination_depth] += count;
    }
    return source;
}

NestedMoveLedger::Index NestedMoveLedger::inserted_before(Index row) const {
    Index count = 0;
    for (Index previous : dumped_) if (previous <= row) ++count;
    return count;
}

NestedMoveLedger::Index NestedMoveLedger::removed_before(const Path& parent, Index row) const {
    Index count = 0;
    auto found = popped_.find(parent);
    if (found != popped_.end())
        for (Index previous : found->second) if (previous <= row) ++count;
    return count;
}

void NestedMoveLedger::commit(const Path& parent, Index source, Index destination) {
    popped_[parent].push_back(source);
    dumped_.push_back(destination);
    records_.emplace_back(parent, source, destination);
}

} // namespace nx::app
