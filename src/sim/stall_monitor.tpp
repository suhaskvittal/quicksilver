/*
 * author: Suhas Vittal
 * date:    25 February 2026
 * */

#include "globals.h"

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
TEMPL_CLASS::STALL_MONITOR(size_t w)
    :window_size(w),
    ticks_until_catchup_(w),
    window_(w,0)
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS void
TEMPL_CLASS::tick(cycle_type c)
{
    cycle_type d = c - last_tick_cycle_;
    assert(d <= N);
    if (ticks_until_catchup_ < d)
    {
        ticks_until_catchup_ = 0;
        for (size_t i = 0; i < d; i++)
            update_stats(window_[i]);

        std::move(window_.begin()+d, window_.end(), window_.begin());
        std::fill(window_.begin() + window_size - d, window_.end(), 0);
        window_start_cycle_++;
    }
    else
    {
        ticks_until_catchup_ -= d;
    }
    last_tick_cycle_ = c;
}

TEMPL_PARAMS void
TEMPL_CLASS::commit_contents()
{
    for (auto x : window_)
        update_stats(x);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS void
TEMPL_CLASS::add_stall_range(T stall_type, cycle_type start, cycle_type end, bool inclusive)
{
    if (inclusive)
        return add_stall_range(stall_type, start, end+1, false);

    if (start < window_start_cycle_)
    {
        std::cerr << "STALL_MONITOR::add_stall_range: start cycle is earlier than the beginning of the window: "
                    << start << " < " << window_start_cycle_ << _die{};
    }

    if (end > window_start_cycle_ + window_size)
    {
        std::cerr << "STALL_MONITOR::add_stall_range: end cycle is later than the end of the window: "
                    << end << " > " << (window_start_cycle_+window_size) << _die{};
    }

    const size_t offset = start - window_start_cycle_;
    const entry_type flag = static_cast<entry_type>( 1 << static_cast<int>(stall_type) );
    for (size_t i = 0; i < end-start; i++)
        window_[i+offset] |= flag;
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
TEMPL_CLASS::update_stats(entry_type _e)
{
    // convert to `int` so we can use popcount and ffs
    int e = static_cast<int>(_e);
    int popcnt = __builtin_popcount(e);
    assert(popcnt <= N);
    
    if (popcnt == 0)
        return;

    if (popcnt == 1)
    {
        // isolated stall:
        int lsb = ffs(e)-1;
        isolated_stalls_[lsb]++;
    }
    total_cycles_with_stalls_++;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#undef TEMPL_PARAMS
#undef TEMPL_CLASS
