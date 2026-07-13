/*
 *  author: Suhas Vittal
 *  date:   14 January 2026
 * */

#ifndef SIM_CONFIGURATION_ALLOCATOR_h
#define SIM_CONFIGURATION_ALLOCATOR_h

#include "sim/production/magic_state.h"

namespace sim
{
namespace configuration
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct Allocation
{
    using array_type = std::vector<std::vector<ProducerBase*>>;

    /*
     * Producers is organized by level (index 0 = L1 production, etc.)
     * */
    array_type producers{};
    size_t     physical_qubit_count{0};
    double     estimated_throughput{0.0};

    Allocation() =default;
    Allocation(const Allocation&) =default;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * `throughput_aware_allocation` is a generic function that provisions
 * production for a given physical qubit budget. This budget is never
 * exceeded.
 *
 * The template parameters are as follows:
 *      1. `Spec` corresponds to the specification that defines a production level. This is
 *          user-defined.
 *      2. `Allocator` is a function that takes in `Spec` and returns a `ProducerBase*`
 *      3. `QubitEstimator` is a function that takes in `Spec` and returns the physical qubit
 *          overhead of allocating a production given that specification.
 *      4. `BandwidthEstimator` is a function that takes in `Spec` and a `double` (the error
 *          rate of the previous level) and returns the resource production rate (in Hz) assuming 
 *          resources from the previous level are always available. If there is no previous level,
 *          the second input is negative (so the function can then set this to some value relative
 *          to `GL_PHYSICAL_ERROR_RATE`).
 *      5. `ConsumptionEstimator` is a function that takes in `Spec` and returns the resource
 *          consumption rate (in Hz)
 *
 * This is a generic function so it works regardless of configuration.
 * The verbosity, in terms of the number of templates, is rather high.
 * We recommend providing wrappers for specific resource states that
 * calls this function to enable ease-of-use.
 * */
template <class Spec, 
            class Allocator, 
            class QubitEstimator,
            class BandwidthEstimator,
            class ConsumptionEstimator>
Allocation throughput_aware_allocation(size_t budget, 
                                        std::vector<Spec>, 
                                        const Allocator&, 
                                        const QubitEstimator&,
                                        const BandwidthEstimator&,
                                        const ConsumptionEstimator&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Computes throughput of the total allocation. `BandwidthEstimator` and `ConsumptionEstimator` are
 * as above for `throughput_aware_allocation`
 * */
template <class Spec, class BandwidthEstimator, class ConsumptionEstimator>
double estimate_throughput_of_allocation(const std::vector<Spec>& specs,
                                            const std::vector<size_t>& counts, 
                                            const BandwidthEstimator&, 
                                            const ConsumptionEstimator&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace configuration
}  // namespace sim

#include "allocator.tpp"

#endif // SIM_CONFIGURATION_ALLOCATOR_h
