/*
 *  author: Suhas Vittal
 *  date:   11 March 2026
 * */

#ifndef SIM_MEMORY_BIVARIATE_BICYCLE_h
#define SIM_MEMORY_BIVARIATE_BICYCLE_h

#include "sim/memory_level.h"
#include "sim/routing/multi_channel_bus.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class BB_MEMORY : public MEMORY_LEVEL
{
public:
    using typename MEMORY_LEVEL::storage_type;

    struct routing_type : public routing::MULTI_CHANNEL_BUS<routing_type>
    {
        routing_type(size_t c, size_t w)
            :MULTI_CHANNEL_BUS(c, w)
        {}
    };

    /*
     * Statistics:
     * */
    uint64_t s_surgery_operations{0};
    uint64_t s_shift_automorphisms{0};
private:
    routing_type routing_;

    /*
     * `adapters_` contains the ready cycle for each BB code adapter.
     * */
    std::vector<cycle_type> adapters_;
public:
    BB_MEMORY(double freq_khz, size_t qubit_count, size_t n, size_t k, size_t d);

    cycle_type next_ready_cycle_for_load(QUBIT*) const override;
private:
    MEMORY_ACCESS_RESULT load_impl(size_t idx, storage_type&, QUBIT*) override;
    MEMORY_ACCESS_RESULT store_impl(size_t idx, storage_type&, QUBIT*) override;
    MEMORY_ACCESS_RESULT coupled_load_store_impl(size_t idx, storage_type&, QUBIT* ld, QUBIT* st) override;

    cycle_type get_latency_of_surgery_operation(size_t idx) const;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#endif // SIM_MEMORY_BIVARIATE_BICYCLE_h
