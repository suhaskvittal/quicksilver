/*
    author: Suhas Vittal
    date:   25 August 2025
*/

#ifndef SIM_ROUTING_h
#define SIM_ROUTING_h

#include <memory>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
    Example of setup:

    J --- RBUS --- J
    |              |
   RBUS           RBUS
    |              |
    J --- RBUS --- J

*/

struct ROUTING_BASE
{
    enum class TYPE { BUS, JUNCTION };

    using ptr_type = std::shared_ptr<ROUTING_BASE>;

    bool                  occupied{false};
    TYPE                  type;
    std::vector<ptr_type> connections;
};

std::vector<ROUTING_BASE::ptr_type> route_path_from_src_to_dst(ROUTING_BASE::ptr_type, ROUTING_BASE::ptr_type);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif