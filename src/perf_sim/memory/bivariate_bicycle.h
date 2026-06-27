/*
 *  author: Suhas Vittal
 *  date:   11 March 2026
 * */

#ifndef SIM_MEMORY_BIVARIATE_BICYCLE_h
#define SIM_MEMORY_BIVARIATE_BICYCLE_h

#include "perf_sim/memory_level.h"
#include "perf_sim/routing/multi_channel_bus.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class BBMemory : public MemoryLevel
{
public:
    using typename MemoryLevel::storage_type;

    struct routing_type : public routing::MultiChannelBus<routing_type>
    {
        routing_type(size_t c, size_t w)
            :MultiChannelBus(c, w)
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
    BBMemory(double freq_khz, size_t qubit_count, size_t n, size_t k, size_t d);

    cycle_type next_ready_cycle_for_load(Qubit*) const override;
    double log_fidelity(Client*, double, double, double) const override;
private:
    MemoryAccessResult load_impl(size_t idx, storage_type&, Qubit*) override;
    MemoryAccessResult store_impl(size_t idx, storage_type&, Qubit*) override;
    MemoryAccessResult coupled_load_store_impl(size_t idx, storage_type&, Qubit* ld, Qubit* st) override;

    cycle_type get_latency_of_surgery_operation(size_t idx) const;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#endif // SIM_MEMORY_BIVARIATE_BICYCLE_h
