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

struct ALLOCATION
{
    using array_type = std::vector<std::vector<PRODUCER_BASE*>>;

    /*
     * Producers is organized by level (index 0 = L1 production, etc.)
     * */
    array_type producers{};
    size_t     physical_qubit_count{0};
    double     estimated_throughput{0.0};

    ALLOCATION() =default;
    ALLOCATION(const ALLOCATION&) =default;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * `throughput_aware_allocation` is a generic function that provisions
 * production for a given physical qubit budget. This budget is never
 * exceeded.
 *
 * The template parameters are as follows:
 *      1. `SPEC_TYPE` corresponds to the specification that defines a production level. This is
 *          user-defined.
 *      2. `ALLOCATOR` is a function that takes in `SPEC_TYPE` and returns a `PRODUCER_BASE*`
 *      3. `QUBIT_ESTIMATOR` is a function that takes in `SPEC_TYPE` and returns the physical qubit
 *          overhead of allocating a production given that specification.
 *      4. `BANDWIDTH_ESTIMATOR` is a function that takes in `SPEC_TYPE` and a `double` (the error
 *          rate of the previous level) and returns the resource production rate (in Hz) assuming 
 *          resources from the previous level are always available. If there is no previous level,
 *          the second input is negative (so the function can then set this to some value relative
 *          to `GL_PHYSICAL_ERROR_RATE`).
 *      5. `CONSUMPTION_ESTIMATOR` is a function that takes in `SPEC_TYPE` and returns the resource
 *          consumption rate (in Hz)
 *
 * This is a generic function so it works regardless of configuration.
 * The verbosity, in terms of the number of templates, is rather high.
 * We recommend providing wrappers for specific resource states that
 * calls this function to enable ease-of-use.
 * */
template <class SPEC_TYPE, 
            class ALLOCATOR, 
            class QUBIT_ESTIMATOR,
            class BANDWIDTH_ESTIMATOR,
            class CONSUMPTION_ESTIMATOR>
ALLOCATION throughput_aware_allocation(size_t budget, 
                                        std::vector<SPEC_TYPE>, 
                                        const ALLOCATOR&, 
                                        const QUBIT_ESTIMATOR&,
                                        const BANDWIDTH_ESTIMATOR&,
                                        const CONSUMPTION_ESTIMATOR&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Computes throughput of the total allocation. `BANDWIDTH_ESTIMATOR` and `CONSUMPTION_ESTIMATOR` are
 * as above for `throughput_aware_allocation`
 * */
template <class SPEC_TYPE, class BANDWIDTH_ESTIMATOR, class CONSUMPTION_ESTIMATOR>
double estimate_throughput_of_allocation(const std::vector<SPEC_TYPE>& specs,
                                            const std::vector<size_t>& counts, 
                                            const BANDWIDTH_ESTIMATOR&, 
                                            const CONSUMPTION_ESTIMATOR&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace configuration
}  // namespace sim

#include "allocator.tpp"

#endif // SIM_CONFIGURATION_ALLOCATOR_h
