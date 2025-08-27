/*
    author: Suhas Vittal
    date:   25 August 2025
*/

#include "routing.h"

#include <algorithm>
#include <iostream>
#include <unordered_map>

extern uint64_t GL_CYCLE;

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<ROUTING_BASE::ptr_type>
route_path_from_src_to_dst(ROUTING_BASE::ptr_type src, ROUTING_BASE::ptr_type dst)
{
    // run dfs to compute the path:
    std::vector<ROUTING_BASE::ptr_type> dfs{src};
    std::unordered_map<ROUTING_BASE::ptr_type, ROUTING_BASE::ptr_type> prev;

    while (dfs.size() > 0)
    {
        auto curr = std::move(dfs.back());
        dfs.pop_back();

        // and exit early if we reach `dst`
        if (curr == dst)
            break;

        // traverse to `connections`
        for (auto& conn : curr->connections)
        {
            // this indicates that we have already visited this node:
            if (prev.find(conn) != prev.end())
                continue;

            if (conn->t_free > GL_CYCLE)
                continue;
            dfs.push_back(conn);
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