/*
 *  author: Suhas Vittal
 *  date:   5 February 2026
 * */

#ifndef SIM_ROUTING_MODEL_MULTI_CHANNEL_BUS_h
#define SIM_ROUTING_MODEL_MULTI_CHANNEL_BUS_h

#include "sim/routing.h"

#include <tuple>
#include <unordered_map>
#include <vector>

namespace sim
{
namespace routing
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * `MultiChannelBus` is a generalization of a routing structure
 * one might often see in FTQCs with topological codes.
 *
 * Each channel can be accessed concurrently.
 *
 * We want to have a "high-level" access mechanism. For example,
 * if I have a pointer to a qubit, then I want to use that pointer
 * to manipulate the routing space. This is generally hard to 
 * implement, so we give this power to the user using a CRTP (see `Impl`).
 *
 * `Impl` should implement a function `translate()` that takes
 * a type of interest and returns `id_type` (see below).
 * The types that need to be supported by `Impl` are those that
 * are potentially passed into:
 *  (1) `set_location()`
 *  (2) `test_and_lock_local_resource()`
 *  (3) `test_and_lock_resources_between()`
 *  etc.
 *
 * Each "C" in the channel is a column, which are organized into
 * rows. Each "entity" in the routing space is defined by a channel,
 * row (0 or 1), and column index (0 to num_resources/2).
 *
 *                CCCCCCCCCCCCCCCCCCCCCCCCC
 * Channel: entry---------------------------entry
 *                CCCCCCCCCCCCCCCCCCCCCCCCC
 * 
 * Each channel can be accessed concurrently.
 * */

constexpr int64_t MCB_LEFT_ENTRY{-1};
constexpr int64_t MCB_RIGHT_ENTRY{-2};

template <class Impl>
class MultiChannelBus
{
public:
    using id_type = int64_t;
    using channel_type = std::vector<Resource>;
    using coord_type = std::tuple<int, int, int>;

    const size_t num_channels;
    const size_t channel_width;
private:
    std::unordered_map<id_type, coord_type> location_map_;
    std::vector<channel_type> channels_;
public:
    MultiChannelBus(size_t num_channels, size_t num_resources_per_channel);

    /*
     * Sets the location of the given object in the routing space.
     * */
    template <class T> 
    void set_location(T, int channel, int row, int column);

    /*
     * Returns true if the requested resources are available.
     * */
    template <class T>
    bool test_local_resource(T, cycle_type from, cycle_type to) const;

    template <class T, class U>
    bool test_resources_between(T, U, cycle_type from, cycle_type to) const;

    /*
     * Attempts to lock the given reosurce. If the resource is not lockable,
     * an error is thrown.
     * */
    template <class T>
    void lock_local_resource(T, cycle_type from, cycle_type to);

    template <class T, class U>
    void lock_resources_between(T, U, cycle_type, cycle_type);

    /*
     * `swap_locations_of()` and `replace()` are useful for
     * handling data movement of any kind.
     *
     * `swap_locations_of()` would be used after some kind of teleportation,
     * so for example after a T gate teleportation, as the program qubit
     * is teleported to a qubit of the last EPR pair.
     *
     * `replace()` would be useful following a memory access.
     * */
    template <class T, class U>
    void swap_locations_of(T, U);

    template <class T, class U>
    void replace(T outgoing, U incoming);

    template <class T>
    const Resource& get_local_resource_ref(T) const;

    /*
     * Calls `Callback` for each routing resource between the two
     * objects. `Callback` is given a `const Resource&`.
     *
     * If `Callback` returns true, then the function exits
     * early.
     * */
    template <class T, class U, class Callback>
    void for_each_resource_between(T, U, const Callback&) const;
private:
    /*
     * This function calls `Impl::translate()`, but only if `T` is not
     * some integral type.
     * */
    template <class T>
    id_type translate(T) const; 

    /*
     * These functions
     * */
    template <class T>
    auto& _get_local_resource_ref(this auto& self, T);

    template <class T, class U, class Callback>
    void _for_each_resource_between(this auto& self, T, U, const Callback&);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace routing
} // namespace sim

#include "multi_channel_bus.tpp"

#endif
