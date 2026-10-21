#include "change_journal.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace nx::app {

ChangeJournal::ChangeJournal() : buffers_(std::make_shared<Buffers>()) {}

void ChangeJournal::enter() {
    if (depth_ == std::numeric_limits<std::int64_t>::max())
        throw std::overflow_error("model comparison nesting is exhausted");
    ++depth_;
}

void ChangeJournal::leave() {
    if (depth_ == std::numeric_limits<std::int64_t>::min())
        throw std::overflow_error("model comparison nesting is exhausted");
    --depth_;
}

void ChangeJournal::mark_primary(const std::string& name) {
    if (buffers_->primary_set.insert(name).second) {
        buffers_->primary.push_back(name);
        buffers_->primary_slots.emplace_back(name);
    }
}

bool ChangeJournal::primary(const std::string& name) const {
    return buffers_->primary_set.count(name) != 0;
}

bool ChangeJournal::forget_primary(const std::string& name) {
    if (!buffers_->primary_set.erase(name)) return false;
    auto& primary = buffers_->primary;
    primary.erase(std::remove(primary.begin(), primary.end(), name), primary.end());
    for (auto& slot : buffers_->primary_slots)
        if (slot && *slot == name) { slot.reset(); break; }
    return true;
}

bool ChangeJournal::pending(const std::string& name) const {
    return buffers_->pending_set.count(name) != 0;
}

void ChangeJournal::capture(const std::string& name) {
    if (buffers_->pending_set.insert(name).second)
        buffers_->pending.push_back(name);
}

void ChangeJournal::forget_pending(const std::string& name) {
    if (!buffers_->pending_set.erase(name)) return;
    auto& pending = buffers_->pending;
    pending.erase(std::remove(pending.begin(), pending.end(), name), pending.end());
}

std::size_t ChangeJournal::pending_count() const { return buffers_->pending.size(); }

const std::vector<std::string>& ChangeJournal::primary_names() const {
    return buffers_->primary;
}

ChangeJournal::PrimaryIterator::PrimaryIterator(std::shared_ptr<Buffers> buffers)
    : buffers_(std::move(buffers)), size_(buffers_->primary.size()), remaining_(size_) {}

std::optional<std::string> ChangeJournal::PrimaryIterator::next() {
    if (exhausted_) return std::nullopt;
    if (size_error_ || buffers_->primary.size() != size_) {
        size_error_ = true;
        throw std::runtime_error("dictionary changed size during iteration");
    }
    while (position_ < buffers_->primary_slots.size()) {
        const auto& slot = buffers_->primary_slots[position_++];
        if (!slot) continue;
        if (!remaining_) {
            exhausted_ = true;
            throw std::runtime_error("dictionary keys changed during iteration");
        }
        --remaining_;
        return *slot;
    }
    exhausted_ = true;
    return std::nullopt;
}

ChangeJournal::PrimaryIterator ChangeJournal::iter_primary() const {
    return PrimaryIterator(buffers_);
}

const std::vector<std::string>& ChangeJournal::pending_names() const {
    return buffers_->pending;
}

std::vector<std::string> ChangeJournal::comparison_order() const {
    std::vector<std::string> result;
    result.reserve(buffers_->pending.size());
    for (const auto& name : buffers_->primary)
        if (pending(name)) result.push_back(name);
    for (const auto& name : buffers_->pending)
        if (!buffers_->primary_set.count(name)) result.push_back(name);
    return result;
}

std::vector<std::string> ChangeJournal::emissions(const std::vector<bool>& changed) const {
    return emissions(comparison_order(), changed);
}

std::vector<std::string> ChangeJournal::emissions(
        const std::vector<std::string>& order, const std::vector<bool>& changed) const {
    if (order.size() != changed.size())
        throw std::invalid_argument("comparison results do not match the change journal");
    bool primary_changed = false;
    for (std::size_t i = 0; i < order.size(); ++i)
        if (buffers_->primary_set.count(order[i]) && changed[i]) primary_changed = true;
    std::vector<std::string> result;
    if (!primary_changed) return result;
    for (std::size_t i = 0; i < order.size(); ++i)
        if (changed[i]) result.push_back(order[i]);
    return result;
}

void ChangeJournal::clear() {
    buffers_->pending.clear();
    buffers_->pending_set.clear();
    clear_primary();
}

void ChangeJournal::clear_primary() {
    buffers_->primary.clear();
    buffers_->primary_set.clear();
    buffers_->primary_slots.clear();
}

ChangeJournal ChangeJournal::deep_copy() const {
    ChangeJournal result;
    result.depth_ = depth_;
    *result.buffers_ = *buffers_;
    return result;
}

} // namespace nx::app
