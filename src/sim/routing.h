/*
    author: Suhas Vittal
    date:   25 August 2025
*/

#ifndef SIM_ROUTING_h
#define SIM_ROUTING_h

#include <memory>
#include <vector>

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

    uint16_t              id;
    TYPE                  type;
    std::vector<ptr_type> connections;
    uint64_t              t_free{0};
};

std::vector<ROUTING_BASE::ptr_type> route_path_from_src_to_dst(ROUTING_BASE::ptr_type, ROUTING_BASE::ptr_type);

std::ostream& operator<<(std::ostream&, const ROUTING_BASE&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif