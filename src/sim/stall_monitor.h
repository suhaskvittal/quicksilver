/*
 *  author: Suhas Vittal
 *  date:   25 February 2026
 * */

#ifndef SIM_STALL_MONITOR_h
#define SIM_STALL_MONITOR_h

#include <array>
#include <cstdint>
#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Tracks what types of stalls have occurred and during which
 * cycle ranges they occur.
 *
 * `N` is the number of possible stalls, and
 * `T` is some class that contains all possible stall
 * types. `T` can be an enum or an integer.
 *
 * Example of how to instantiate this class:
 *  ```
 *      enum class STALL_TYPE { RESOURCE=0, MEMORY=1, SIZE=2 };
 *      STALL_MONITOR<static_cast<int>(STALL_TYPE::SIZE), STALL_TYPE> sm;
 *  ```
 *
 * Obviously, given that the above is rather verbose, we recommend
 * doing a typedef.
 * */

template <size_t N, class T>
class STALL_MONITOR
{
public:
    /*
     * TODO: redefine based off of the value of `N`
     * */
    using entry_type = uint8_t;
    using stall_stats_type = std::array<uint64_t, N>;

    const size_t max_ranges;
private:
    /*
     * A `stall_range` represents a contiguous span of cycles during which
     * a fixed combination of stalls (encoded as a bitmask in `flags`) is
     * active. The interval is half-open: [start, end).
     *
     * Invariant: `ranges_` is kept sorted by `start` and contains
     * non-overlapping intervals at all times.
     * */
    struct stall_range
    {
        cycle_type start;
        cycle_type end;    // exclusive
        entry_type flags;
    };

    std::vector<stall_range> ranges_;

    /*
     * All cycles strictly before `committed_up_to_` have already been
     * folded into `isolated_stalls_` / `total_cycles_with_stalls_`.
     * `add_stall_range` rejects any range that starts before this value.
     * */
    cycle_type committed_up_to_{0};

    /*
     * Number of isolated stalls by type.
     * */
    stall_stats_type isolated_stalls_{};
    uint64_t         total_cycles_with_stalls_{0};
public:
    STALL_MONITOR(size_t max_ranges);

    /*
     * This should be called at the end of simulation, as this
     * consumes the remaining contents of `ranges_`.
     * */
    void commit_contents();

    /*
     * Adds stalls of the given type to all cycles within the
     * range `start` to `end`.
     *
     * If `start` is earlier than `committed_up_to_`, an error
     * is thrown and the program exits.
     * */
    void add_stall_range(T, cycle_type start, cycle_type end, bool inclusive);

    /*
     * Returns the number of isolated stall cycles for the given type.
     * */
    uint64_t isolated_stalls_for(T) const;

    /*
     * Returns the total number of stall cycles (isolated or not).
     * */
    uint64_t cycles_with_stalls() const;
private:
    /*
     * Folds a single stall_range into the running stats.
     * */
    void commit_range(const stall_range&);

    /*
     * If `ranges_.size() > max_ranges`, commits the earliest ranges
     * until the count is within the limit.
     * */
    void evict_if_needed();
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#include "stall_monitor.tpp"

#endif  // SIM_STALL_MONITOR_h
