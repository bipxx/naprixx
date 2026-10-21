#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace nx::app {

// Python-independent names/depth. Native model owners retain Python values.
class ChangeJournal {
    struct Buffers;
public:
    class PrimaryIterator {
    public:
        std::optional<std::string> next();
    private:
        friend class ChangeJournal;
        explicit PrimaryIterator(std::shared_ptr<Buffers> buffers);
        std::shared_ptr<Buffers> buffers_;
        std::size_t size_, remaining_, position_ = 0;
        bool exhausted_ = false, size_error_ = false;
    };
    ChangeJournal();
    void enter();
    void leave();
    std::int64_t depth() const { return depth_; }
    void set_depth(std::int64_t value) { depth_ = value; }
    void mark_primary(const std::string& name);
    bool primary(const std::string& name) const;
    bool forget_primary(const std::string& name);
    bool pending(const std::string& name) const;
    void capture(const std::string& name);
    void forget_pending(const std::string& name);
    std::size_t pending_count() const;
    const std::vector<std::string>& primary_names() const;
    PrimaryIterator iter_primary() const;
    const std::vector<std::string>& pending_names() const;
    std::vector<std::string> comparison_order() const;
    std::vector<std::string> emissions(const std::vector<bool>& changed) const;
    std::vector<std::string> emissions(const std::vector<std::string>& order,
                                       const std::vector<bool>& changed) const;
    void clear();
    void clear_primary();
    ChangeJournal deep_copy() const;
    void share_buffers(const ChangeJournal& source) { buffers_ = source.buffers_; }

private:
    struct Buffers {
        std::vector<std::string> primary, pending;
        std::unordered_set<std::string> primary_set, pending_set;
        // Deleted slots remain until clear, preserving a live iterator's
        // position when an already-consumed key is removed and replaced.
        std::vector<std::optional<std::string>> primary_slots;
    };
    // Pydantic shallow copies share queue containers but copy the delay depth.
    std::shared_ptr<Buffers> buffers_;
    std::int64_t depth_ = 0;
};

} // namespace nx::app
