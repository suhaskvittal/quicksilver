/*
 *  author: Suhas Vittal
 *  date:   11 March 2026
 * */

#ifndef SIM_MEMORY_BIVARIATE_BICYCLE_h
#define SIM_MEMORY_BIVARIATE_BICYCLE_h

#include "sim/memory/bivariate_bicycle.h"
#include "sim/routing/multi_channel_bus.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class BB_MEMORY : public MEMORY_LEVEL<BB_MEMORY>
{
public:
    using typename MEMORY_LEVEL<BB_MEMORY>::storage_type;

    struct routing_type : public routing::MULTI_CHANNEL_BUS<routing_type>
    {
    };

    /*
     * Statistics:
     * */
    uint64_t s_surgery_operations{0};
    uint64_t s_shift_automorphisms{0};
private:
    routing_type routing_;
public:
    BB_MEMORY(double freq_khz, size_t n, size_t k, size_t d);
private:
    MEMORY_ACCESS_RESULT load_impl(size_t idx, storage_type&, QUBIT*);
    MEMORY_ACCESS_RESULT store_impl(size_t idx, storage_type&, QUBIT*);
    MEMORY_ACCESS_RESULT coupled_load_store_impl(size_t idx, storage_type&, QUBIT*, QUBIT*);

    cycle_type get_next_ready_cycle_flor_load_impl(QUBIT*) const;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#endif // SIM_MEMORY_BIVARIATE_BICYCLE_h
