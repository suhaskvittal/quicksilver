/*
    author: Suhas Vittal
    date:   25 August 2025
*/

#include "routing.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <unordered_map>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ROUTING_COMPONENT::ptr_type
find_next_available_bus(const PATCH& p)
{
    auto it = std::min_element(p.buses.begin(), p.buses.end(),
                                [](const ROUTING_COMPONENT::ptr_type& a, const ROUTING_COMPONENT::ptr_type& b) 
                                { 
                                    return a->cycle_free < b->cycle_free;
                                });
    return *it;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

routing_result_type
route_path_from_src_to_dst(ROUTING_COMPONENT::ptr_type src, ROUTING_COMPONENT::ptr_type dst, uint64_t start_cycle)
{
    routing_path_type path;
    uint64_t next_start_cycle;
    std::tie(path, next_start_cycle) = route_path_helper(src, dst, start_cycle);
    while (path.empty() && next_start_cycle < std::numeric_limits<uint64_t>::max())
    {
        start_cycle = next_start_cycle;
        std::tie(path, next_start_cycle) = route_path_helper(src, dst, start_cycle);
    }

    if (path.empty())
        throw std::runtime_error("failed to route path");

    return routing_result_type{path, start_cycle};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

routing_result_type
route_path_helper(ROUTING_COMPONENT::ptr_type src, ROUTING_COMPONENT::ptr_type dst, uint64_t start_cycle)
{
    if (src == dst)
        return routing_result_type{{src}, start_cycle};

    // run bfs to compute the path:
    std::deque<ROUTING_COMPONENT::ptr_type> bfs{src};
    std::unordered_map<ROUTING_COMPONENT::ptr_type, ROUTING_COMPONENT::ptr_type> prev;

    uint64_t next_smallest_cycle_free = std::numeric_limits<uint64_t>::max();

    while (bfs.size() > 0)
    {
        auto curr = std::move(bfs.front());
        bfs.pop_front();

        // and exit early if we reach `dst`
        if (curr == dst)
            break;

        // traverse to `connections`
        for (const auto& conn : curr->connections)
        {
            // this indicates that we have already visited this node:
            if (prev.find(conn) != prev.end())
                continue;

            // this indicates that the connection is blocked:
            if (conn->cycle_free > start_cycle)
            {
                next_smallest_cycle_free = std::min(next_smallest_cycle_free, conn->cycle_free);
                continue;
            }

            bfs.push_back(conn);
            prev[conn] = curr;
        }
    }

    // backtrack from `dst`
    std::vector<ROUTING_COMPONENT::ptr_type> path;

    // only continue if `dst` has been visited
    auto it = prev.find(dst);
    if (it != prev.end())
    {
        path.push_back(dst);
        auto& curr = it->second;
        while (curr != src)
        {
            path.push_back(curr);
            curr = prev[curr];
        }
        path.push_back(src);
        std::reverse(path.begin(), path.end());
    }

    return routing_result_type{path, next_smallest_cycle_free};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim