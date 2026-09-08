/*
 *  author: Suhas Vittal
 *  date:   14 January 2026
 * */

#ifndef SIM_CONFIGURATION_ALLOCATOR_h
#define SIM_CONFIGURATION_ALLOCATOR_h

#include "sim/production.h"

#include <utility>
#include <vector>

namespace sim
{
namespace configuration
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct Allocation
{
    using level_type = std::pair<ProducerQuantum, size_t>;  // (quantum, producer count)
    using array_type = std::vector<level_type>;

    /*
     * `levels` is organized by level (index 0 = L1 production, etc.). Each entry is fed
     * directly to `BulkProducer::add_level`.
     * */
    array_type levels{};
    size_t     physical_qubit_count{0};
    double     estimated_throughput{0.0};

    Allocation() =default;
    Allocation(const Allocation&) =default;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * `throughput_aware_allocation` provisions a stack of `BulkProducer` levels for a given
 * physical qubit budget. This budget is never exceeded.
 *
 * `quanta` describes each production level (index 0 = L1, closest to state injection). Level
 * i's `ProducerQuantum` is expected to already account for the infidelity of level i-1 (see
 * e.g. `t_15to1_distillation`'s `input_state_infidelity` argument) -- this function only
 * decides *how many* producers to allocate at each level, not their per-unit characteristics.
 *
 * `freq_khz` is the clock the resulting `BulkProducer` will run at; `ProducerQuantum::cycles_per_production`
 * is interpreted at this frequency.
 * */
Allocation throughput_aware_allocation(size_t budget, std::vector<ProducerQuantum> quanta, double freq_khz);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Computes the throughput (Hz) of the top level of an allocation with the given per-level
 * producer `counts`, accounting for production/consumption rate mismatches between levels.
 * */
double estimate_throughput_of_allocation(const std::vector<ProducerQuantum>& quanta,
                                            const std::vector<size_t>& counts,
                                            double freq_khz);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace configuration
}  // namespace sim

#endif // SIM_CONFIGURATION_ALLOCATOR_h
