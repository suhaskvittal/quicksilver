/*
 *  author: Suhas Vittal
 *  date:   25 February 2026
 * */

#ifndef SIM_STALL_MONITOR_h
#define SIM_STALL_MONITOR_h

#include <cstdint>
#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Tracks what types of stalls have occurred at each
 * program cycle for a user-defined window of cycles.
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

    const size_t window_size;
private:
    /*
     * The cycle that corresponds to the beginning of the
     * window.
     * */
    cycle_type window_start_cycle_{0};

    /*
     * At the start of simulation, the owner's cycle will
     * be 0, but this monitor will have entries corresponding
     * to future cycles. These entries will be empty.
     *
     * Eventually, the owner's cycle will hit the end of the
     * window, which is the steady state. This variable
     * contains the number of calls to `tick()` until this
     * occurs.
     * */
    cycle_type ticks_until_catchup_;

    /*
     * Last cycle passed to `tick()`
     * */
    cycle_type last_tick_cycle_{0};

    /*
     * Info window:
     *  Each entry is an integer that contains flags. The
     *  k-th bit is set if there was a stall corresponding
     *  to the k-th value of `T`.
     * */
    std::vector<uint8_t> window_;

    /*
     * Number of isolated stalls by type.
     * */
    stall_stats_type isolated_stalls_{};
    uint64_t         total_cycles_with_stalls_{0};
public:
    STALL_MONITOR(size_t window_size);

    /*
     * Moves forward to the given cycle and adjusts the window accordingly.
     * This should be called on every call to `operate()` of the owner.
     * */
    void tick(cycle_type);
    
    /*
     * This should be called at the end of simulation, as this
     * consumes the remaining contents of the window.
     * */
    void commit_contents();

    /*
     * Adds stalls of the given type to all cycles within the
     * range `start` to `end`.
     *
     * If `start` or `end` exceed the range of the window, an
     * error is thrown and the program exits.
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
    void update_stats(entry_type);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#include "stall_monitor.tpp"

#endif  // SIM_STALL_MONITOR_h
