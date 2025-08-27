/*
    author: Suhas Vittal
    date:   25 August 2025
*/

#include "routing.h"

#include <algorithm>
#include <deque>
#include <iostream>
#include <unordered_map>

extern uint64_t GL_CYCLE;

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<ROUTING_BASE::ptr_type>
route_path_from_src_to_dst(ROUTING_BASE::ptr_type src, ROUTING_BASE::ptr_type dst, uint64_t current_cycle)
{
    // run bfs to compute the path:
    std::deque<ROUTING_BASE::ptr_type> bfs{src};
    std::unordered_map<ROUTING_BASE::ptr_type, ROUTING_BASE::ptr_type> prev;

    while (bfs.size() > 0)
    {
        auto curr = std::move(bfs.front());
        bfs.pop_front();

        // and exit early if we reach `dst`
        if (curr == dst)
            break;

        // traverse to `connections`
        for (auto& conn : curr->connections)
        {
            // this indicates that we have already visited this node:
            if (prev.find(conn) != prev.end())
                continue;

            // this indicates that the connection is blocked:
            if (conn->t_free > current_cycle)
                continue;

            bfs.push_back(conn);
            prev[conn] = curr;
        }
    }

    // backtrack from `dst`
    std::vector<ROUTING_BASE::ptr_type> path;

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
    return path;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::ostream& 
operator<<(std::ostream& os, const ROUTING_BASE& r)
{
    constexpr std::string_view TYPE_STRING[] = { "b", "j" };
    os << "ROUTING_BASE(" << TYPE_STRING[static_cast<size_t>(r.type)] << r.id << ")";
    return os;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim