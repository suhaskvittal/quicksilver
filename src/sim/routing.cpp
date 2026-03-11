/*
 *  author: Suhas Vittal
 *  date:   10 March 2026
 * */

#include "sim/routing.h"

namespace sim
{
namespace routing
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

template <class ITER>
bool _check_for_intersection(range_type, ITER begin, ITER end);

}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

RESOURCE::RESOURCE()
{
    usage_.reserve(RANGE_LIMIT);
}

void
RESOURCE::lock_for_time_interval(cycle_type a, cycle_type b)
{
    range_type r{a,b};
    if (_check_for_intersection(r, usage_.begin(), usage_.end()))
    {
        std::cerr << "RESOURCE::lock_for_time_interval: resource is not free from cycle " 
            << a << " to " << b << _die{};
    }

    auto it = std::find_if(usage_.begin(), usage_.end(), [a] (const auto& r) { return a < r.first; });
    if (usage_.size() >= RANGE_LIMIT)
    {
        if (it != usage_.begin())
        {
            std::move(usage_.begin()+1, it, usage_.begin());
            *(it-1) = r;
        }
        else
        {
            std::cerr << "RESOURCE::lock_for_time_interval: range is not tracked"
                     << " -- consider increasing RANGE_LIMIT\n";
        }
    }
    else
    {
        usage_.insert(it, r);
    }
}

bool
RESOURCE::is_lockable(cycle_type a, cycle_type b)
{
    return !_check_for_instruction(range_type{a,b}, usage_.begin(), usage_.end());
}

cycle_type
next_ready_cycle(cycle_type current_cycle, cycle_type t) const
{
    cycle_type c{current_cycle};
    for (size_t i = 0; i < usage_.size(); i++)
    {
        const auto& [a, b] = usage_.at(i);
        if (c+t < a)
            return c;
        else
            c = b;
    }
    return c;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

template <class ITER> bool
_check_for_intersection(range_type r, ITER begin, ITER end)
{
    return std::any_of(begin, end,
                [a=r.first, b=r.second] (const auto& s)
                {
                    const auto& [x,y] = s;
                    const bool r_contains_s = (a <= x && b >= y),
                               s_contains_r = (a >= x && b <= y),
                               partial = (a >= x && a <= y) || (b >= x && b <= y);
                    return r_contains_s || s_contains_r || partial;
                });
}

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace routing
} // namespace sim
