/*
 *  author: Suhas Vittal
 *  date:   5 February 2026
 * */

#ifndef SIM_ROUTING_MODEL_h
#define SIM_ROUTING_MODEL_h

#include "globals.h"

#include <vector>
#include <utility>

namespace sim
{
namespace routing
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * A routing "resource". This is essentially something that can
 * be locked for some period of time.
 *
 * A simple way of thinking of a resource is like an ancilla surface
 * code patch used for routing. This is a resource that can be locked
 * for some given amount of time.
 * */
class Resource
{
public:
    constexpr static size_t RANGE_LIMIT{8};

    /*
     * We use ranges to determine when a routing resource is locked down.
     * */
    using range_type = std::pair<cycle_type, cycle_type>;
private:
    std::vector<range_type> usage_{};
public:
    Resource();
    Resource(const Resource&) =default;

    /*
     * Locks the routing resource for the given time interval and throws
     * and error if it is not lockable.
     * */
    void lock_for_time_interval(cycle_type, cycle_type);

    /*
     * Determines whether the given resource can be locked for the given
     * time range. Returns `false` if this is not possible.
     * */
    bool is_lockable(cycle_type from, cycle_type to) const;

    /*
     * Returns the next cycle where the resource can be locked for
     * `desired_lock_time` cycles.
     * */
    cycle_type next_ready_cycle(cycle_type current_cycle, cycle_type desired_lock_time) const;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace routing
} // namespace sim

#endif // SIM_ROUTING_MODEL_h
