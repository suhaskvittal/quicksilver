/*
    author: Suhas Vittal
    date:   25 August 2025
*/

#ifndef SIM_ROUTING_h
#define SIM_ROUTING_h

#include "sim/client.h"

#include <memory>
#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct ROUTING_COMPONENT
{
    using ptr_type = std::shared_ptr<ROUTING_COMPONENT>;

    std::vector<ptr_type> connections;
    uint64_t              cycle_free{0};
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct PATCH
{
    bool is_prefetched{false};
    size_t num_uses{0};

    QUBIT contents{-1,-1};
    std::vector<ROUTING_COMPONENT::ptr_type> buses;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// this is the path and the time at which the path can first be used
using routing_path_type = std::vector<ROUTING_COMPONENT::ptr_type>;
using routing_result_type = std::pair<routing_path_type, uint64_t>;

ROUTING_COMPONENT::ptr_type find_next_available_bus(const PATCH& p);

// unlike `route_path_helper`, the path is never empty
routing_result_type         route_path_from_src_to_dst(ROUTING_COMPONENT::ptr_type src, ROUTING_COMPONENT::ptr_type dst, uint64_t start_cycle);

// this is the helper function for `route_path_from_src_to_dst`
// if we fail to route, the path is empty
// the second element of the result is the next time where a routing component is free
routing_result_type         route_path_helper(ROUTING_COMPONENT::ptr_type src, ROUTING_COMPONENT::ptr_type dst, uint64_t start_cycle);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif