/*
 * author: Suhas Vittal
 * date:    25 February 2026
 * */

#include "globals.h"

#include <algorithm>
#include <cassert>
#include <iostream>

#include <strings.h>

#define TEMPL_PARAMS    template <size_t N, class T>
#define TEMPL_CLASS     STALL_MONITOR<N,T>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS
TEMPL_CLASS::STALL_MONITOR(size_t _max_ranges)
    :max_ranges(_max_ranges)
{
    ranges_.reserve(_max_ranges);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS void
TEMPL_CLASS::commit_contents()
{
    for (const auto& r : ranges_)
        commit_range(r);
    ranges_.clear();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS void
TEMPL_CLASS::add_stall_range(T stall_type, cycle_type start, cycle_type end, bool inclusive)
{
    if (inclusive)
        return add_stall_range(stall_type, start, end+1, false);

    if (start >= end)
        return;

    if (start < committed_up_to_)
    {
        std::cerr << "STALL_MONITOR::add_stall_range: start cycle is earlier than committed cycle: "
                    << start << " < " << committed_up_to_ << _die{};
    }

    const entry_type f = static_cast<entry_type>( 1 << static_cast<int>(stall_type) );

    /*
     * Find the slice of `ranges_` that overlaps with [start, end).
     * Two half-open intervals [a,b) and [c,d) overlap iff a < d && c < b.
     *
     * lo = first interval with .end > start  (not entirely before us)
     * hi = first interval with .start >= end (entirely after us)
     * */
    auto lo = std::lower_bound(ranges_.begin(), ranges_.end(), start,
                    [](const stall_range& r, cycle_type v) { return r.end <= v; });
    auto hi = std::lower_bound(lo, ranges_.end(), end,
                    [](const stall_range& r, cycle_type v) { return r.start < v; });

    /*
     * Build `new_parts`: the replacement for the intervals in [lo, hi),
     * incorporating the new [start, end) with flag `f`.
     *
     * We walk through the overlapping intervals and split each into:
     *   - a left tail   [I.start, cursor)  with only I.flags  (if I started before cursor)
     *   - a gap         [cursor, I.start)  with only f        (if there is a gap before I)
     *   - an overlap    [cursor, ov_end)   with I.flags | f
     *   - a right tail  [end, I.end)       with only I.flags  (if I extends past end)
     * */
    std::vector<stall_range> new_parts;
    cycle_type cursor = start;

    for (auto it = lo; it != hi; ++it)
    {
        // Gap between cursor and the start of this interval: new range only.
        if (cursor < it->start)
        {
            new_parts.push_back({cursor, it->start, f});
            cursor = it->start;
        }

        // Left tail of this interval that precedes cursor (i.e., the interval
        // started before `start`): existing flags only.
        if (it->start < cursor)
            new_parts.push_back({it->start, cursor, it->flags});

        // Merged overlap region.
        const cycle_type ov_end = std::min(it->end, end);
        new_parts.push_back({cursor, ov_end, static_cast<entry_type>(it->flags | f)});
        cursor = ov_end;

        // Right tail of this interval that extends past `end`: existing flags only.
        if (it->end > end)
            new_parts.push_back({end, it->end, it->flags});
    }

    // Any remaining portion of [start, end) not covered by existing intervals.
    if (cursor < end)
        new_parts.push_back({cursor, end, f});

    // Merge adjacent new_parts entries that share the same flags.
    for (size_t i = 0; i + 1 < new_parts.size(); )
    {
        if (new_parts[i].end == new_parts[i+1].start
                && new_parts[i].flags == new_parts[i+1].flags)
        {
            new_parts[i].end = new_parts[i+1].end;
            new_parts.erase(new_parts.begin() + i + 1);
        }
        else
        {
            ++i;
        }
    }

    // Replace [lo, hi) with new_parts.
    const size_t lo_idx = static_cast<size_t>(lo - ranges_.begin());
    ranges_.erase(lo, hi);
    ranges_.insert(ranges_.begin() + lo_idx, new_parts.begin(), new_parts.end());

    /*
     * `end_idx` is one-past the last inserted element. If the left-boundary
     * merge fires it erases ranges_[lo_idx], shifting every subsequent index
     * by -1, so we decrement end_idx to compensate before the right-boundary
     * check.
     * */
    size_t end_idx = lo_idx + new_parts.size();

    // Merge with the interval immediately before the insertion point (if any).
    if (lo_idx > 0)
    {
        auto& prev = ranges_[lo_idx - 1];
        auto& first = ranges_[lo_idx];
        if (prev.end == first.start && prev.flags == first.flags)
        {
            prev.end = first.end;
            ranges_.erase(ranges_.begin() + lo_idx);
            end_idx--;
        }
    }

    // Merge the last inserted interval with the interval immediately following (if any).
    if (end_idx > 0 && end_idx < ranges_.size())
    {
        auto& last = ranges_[end_idx - 1];
        auto& next = ranges_[end_idx];
        if (last.end == next.start && last.flags == next.flags)
        {
            last.end = next.end;
            ranges_.erase(ranges_.begin() + end_idx);
        }
    }

    evict_if_needed();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS uint64_t
TEMPL_CLASS::isolated_stalls_for(T stall_type) const
{
    return isolated_stalls_.at(static_cast<int>(stall_type));
}

TEMPL_PARAMS uint64_t
TEMPL_CLASS::cycles_with_stalls() const
{
    return total_cycles_with_stalls_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS void
TEMPL_CLASS::commit_range(const stall_range& r)
{
    const uint64_t cycle_count = r.end - r.start;
    // convert to `int` so we can use popcount and ffs
    const int e = static_cast<int>(r.flags);
    const int popcnt = __builtin_popcount(e);
    assert(popcnt <= static_cast<int>(N));

    if (popcnt == 0)
        return;

    if (popcnt == 1)
        isolated_stalls_[ffs(e)-1] += cycle_count;
    total_cycles_with_stalls_ += cycle_count;
}

TEMPL_PARAMS void
TEMPL_CLASS::evict_if_needed()
{
    while (ranges_.size() > max_ranges)
    {
        commit_range(ranges_.front());
        committed_up_to_ = std::max(committed_up_to_, ranges_.front().end);
        ranges_.erase(ranges_.begin());
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#undef TEMPL_PARAMS
#undef TEMPL_CLASS
