/*
 *  author: Suhas Vittal
 *  date:   5 February 2026
 *  contributor: OpenAI Codex (RoutingBase migration, 30 September 2026)
 *  contributor: OpenAI Codex (resource count return, 10 November 2026)
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
 * `Translator` converts application objects to routing IDs. It must
 * implement `translate()` for nonintegral types passed into:
 *  (1) `set_location()`
 *  (2) `test_local_resource()` and `lock_local_resource()`
 *  (3) `test_resources_between()` and `lock_resources_between()`
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

template <class Translator>
class MultiChannelBus : public sim::RoutingBase<Translator>
{
public:
    using channel_type = std::vector<Resource>;
    using coord_type = std::tuple<int, int, int>;

    const size_t num_channels;
    const size_t channel_width;
private:
    std::unordered_map<id_type, coord_type> location_map_;
    std::vector<channel_type> channels_;
public:
    MultiChannelBus(size_t num_channels, size_t num_resources_per_channel);
    MultiChannelBus(size_t num_channels, size_t num_resources_per_channel, Translator);

    /*
     * Sets the location of the given object in the routing space.
     * */
    template <class T> 
    void set_location(T, int channel, int row, int column);

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

    /*
     * Returns number of patches between two points.
     * */
    template <class T, class U>
    size_t patch_distance(T, U) const;

    /* Calls `Callback` for each routing resource between the objects.
     * The time interval is accepted for the common layout interface; the
     * bus selects its path independently of resource availability. */
    template <class T, class U, class Callback>
    void for_each_resource_between(T, U, cycle_type from, cycle_type to, const Callback&) const;

    template <class T, class U, class Callback>
    void for_each_resource_between(T, U, const Callback&) const;
private:
    bool _test_local_resource(id_type, cycle_type, cycle_type) const override;
    bool _test_resources_between(id_type, id_type, cycle_type, cycle_type) const override;
    void _lock_local_resource(id_type, cycle_type, cycle_type) override;
    size_t _lock_resources_between(id_type, id_type, cycle_type, cycle_type) override;

    template <class T>
    auto& get_local_resource_ref(this auto& self, T);

    template <class Callback>
    void _for_each_resource_between(this auto& self, id_type, id_type, const Callback&);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace routing
} // namespace sim

#include "multi_channel_bus.tpp"

#endif
