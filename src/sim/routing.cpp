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

using range_type = Resource::range_type;

template <class Iter>
bool _check_for_intersection(range_type, Iter begin, Iter end);

}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

Resource::Resource()
{
    usage_.reserve(RANGE_LIMIT);
}

void
Resource::lock_for_time_interval(cycle_type a, cycle_type b)
{
    range_type r{a,b};
    if (_check_for_intersection(r, usage_.begin(), usage_.end()))
    {
        std::cerr << "Resource::lock_for_time_interval: resource is not free from cycle " 
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
            std::cerr << "Resource::lock_for_time_interval: range is not tracked"
                     << " -- consider increasing RANGE_LIMIT\n";
        }
    }
    else
    {
        usage_.insert(it, r);
    }
}

bool
Resource::is_lockable(cycle_type a, cycle_type b) const
{
    return !_check_for_intersection(range_type{a,b}, usage_.begin(), usage_.end());
}

cycle_type
Resource::next_ready_cycle(cycle_type current_cycle, cycle_type t) const
{
    cycle_type c{current_cycle};
    for (size_t i = 0; i < usage_.size(); i++)
    {
        const auto& [a, b] = usage_.at(i);
        if (b < c)
            continue;
        // Ranges are half-open [a, b): a duration-t lock occupies [c, c+t), so
        // it fits before this range as long as c+t <= a (adjacency is allowed).
        if (c+t <= a)
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

template <class Iter> bool
_check_for_intersection(range_type r, Iter begin, Iter end)
{
    r.second--;
    return std::any_of(begin, end,
                [a=r.first, b=r.second] (const auto& s)
                {
                    auto [x,y] = s;
                    y--;
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
