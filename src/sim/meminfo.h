/*
    author: Suhas Vittal
    date:   25 August 2025
*/

#ifndef SIM_MEMINFO_h
#define SIM_MEMINFO_h

#include "sim/routing.h"

#include <cstdint>
#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct MEMINFO
{
    enum class LOCATION { COMPUTE, MEMORY };

    uint8_t    client_id;
    qubit_type qubit_id;
    LOCATION   where;
    size_t     patch_idx;
    size_t     memory_idx;

    uint64_t   t_free{0};
};

struct PATCH
{
    using bus_array = std::vector<ROUTING_BASE::ptr_type>;

    bus_array buses;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif