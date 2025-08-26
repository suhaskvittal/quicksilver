/*
    author: Suhas Vittal
    date:   25 August 2025
*/

#include "routing.h"

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
        auto& curr = dfs.back();
        dfs.pop_back();

        // this indicates that we have already visited this node:
        if (prev.find(curr) != prev.end())
            continue;

        // and exit early if we reach `dst`
        if (curr == dst)
            break;

        // traverse to `connections`
        for (auto& conn : curr->connections)
        {
            if (conn->occupied)
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

        auto& curr = *it;
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

}   // namespace sim