/*
 *  author: Suhas Vittal
 *  date:   5 February 2026
 *  contributor: OpenAI Codex (RoutingSpace interface, 30 September 2026)
 *  contributor: OpenAI Codex (resource count return, 10 November 2026)
 * */

#ifndef SIM_ROUTING_MODEL_h
#define SIM_ROUTING_MODEL_h

#include "globals.h"

#include <cstdint>
#include <vector>
#include <utility>

namespace sim
{
namespace routing
{

using id_type = int64_t;

} // namespace routing

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Translates application objects to routing IDs before dispatching to a
 * concrete routing layout. Integral values are already routing IDs.
 *
 * Translator may carry state, so it is stored separately from the layout.
 * Layouts implement only the ID-based virtual operations below.
 * */
template <class Translator>
class RoutingSpace
{
public:
    RoutingSpace();
    explicit RoutingSpace(Translator translator);
    virtual ~RoutingSpace();

    template <class T>
    routing::id_type translate(T obj) const;

    template <class T>
    bool test_local_resource(T obj, cycle_type from, cycle_type to) const;

    template <class T, class U>
    bool test_resources_between(T src, U dst, cycle_type from, cycle_type to) const;

    template <class T>
    void lock_local_resource(T obj, cycle_type from, cycle_type to);

    template <class T, class U>
    size_t lock_resources_between(T src, U dst, cycle_type from, cycle_type to);

protected:
    virtual bool _test_local_resource(routing::id_type, cycle_type, cycle_type) const = 0;
    virtual bool _test_resources_between(routing::id_type, routing::id_type, cycle_type, cycle_type) const = 0;
    virtual void _lock_local_resource(routing::id_type, cycle_type, cycle_type) = 0;
    virtual size_t _lock_resources_between(routing::id_type, routing::id_type, cycle_type, cycle_type) = 0;

private:
    Translator translator_;
};

template <class Translator>
using RoutingBase = RoutingSpace<Translator>;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace routing
{

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

#include "routing.tpp"

#endif // SIM_ROUTING_MODEL_h
