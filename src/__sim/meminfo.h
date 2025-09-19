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

    int8_t     client_id;
    qubit_type qubit_id;
    LOCATION   where;

    uint64_t   t_free{0};
};

struct PATCH
{
    using bus_array = std::vector<ROUTING_BASE::ptr_type>;

    int8_t     client_id{-1};
    qubit_type qubit_id{-1};
    bus_array  buses;

    bool has_program_qubit() const { return client_id >= 0 && qubit_id >= 0; }
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif