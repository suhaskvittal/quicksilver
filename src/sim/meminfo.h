/*
    author: Suhas Vittal
    date:   25 August 2025
*/

#ifndef SIM_MEMINFO_h
#define SIM_MEMINFO_h

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct MEMINFO
{
    enum class LOCATION { COMPUTE, MEMORY, PENDING_XFER_MEM_TO_CMP, PENDING_XFER_CMP_TO_MEM };

    uint8_t    client_id;
    qubit_type qubit_id;
    LOCATION   where;
    size_t     patch_idx;
    size_t     memory_idx;

    uint64_t   t_free{0};
    uint64_t   t_until_in_compute{0};
    uint64_t   t_until_in_memory{0};
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