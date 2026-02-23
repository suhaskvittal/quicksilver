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

    FACTORY_ALLOCATION() =default;
    FACTORY_ALLOCATION(const FACTORY_ALLOCATION&) =default;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct FACTORY_SPECIFICATION
{
    bool is_cultivation{false};

    uint64_t syndrome_extraction_round_time_ns{1200};
    size_t   buffer_capacity;
    double   output_error_rate;

    /*
     * Cultivation variables (defaults are for d = 3 color code cultivation)
     *  -- `escape_distance` is the final distance of the cultivated state
     *  -- `round_length` is the number of rounds required to cultivate the state
     *  -- `probability_of_success` is the probability of the cultivated state not being discarded
     * */
    size_t escape_distance{13};
    size_t rounds{25};
    double probability_of_success{0.2};

    /*
     * Distillation variables (defaults are for 15:1 (25,11,11) distillation)
     * */
    size_t dx{25};
    size_t dz{11};
    size_t dm{11};
    size_t input_count{4};
    size_t output_count{1};
    size_t rotations{11};
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
 *      2. `ALLOCATOR` is a function that takes in `SPEC_TYPE` and returns a `PRODUCER_BASE`
 *      3. `QUBIT_ESTIMATOR` is a function that takes in `SPEC_TYPE` and returns the physical qubit
 *          overhead of allocating a production given that specification.
 *      4. `BANDWIDTH_ESTIMATOR` is a function that takes in `SPEC_TYPE` and returns the resource
 *          production rate (in Hz) assuming resources from the previous level are always available.
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
template <class BANDWIDTH_ESTIMATOR, class CONSUMPTION_ESTIMATOR>
double estimate_throughput_of_allocation(const ALLOCATION::array_type&, 
                                            const BANDWIDTH_ESTIMATOR&, 
                                            const CONSUMPTION_ESTIMATOR&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * If there is only a single level of factories, then it is as simple as dividing
 * `physical_qubit_budget` by the overhead per factory.
 * */
FACTORY_ALLOCATION l1_factory_allocation(size_t physical_qubit_budget, FACTORY_SPECIFICATION);

/*
 * Allocates a factory within a given physical qubit budget.
 *
 * Since we target an error rate of 1e-12 per magic state,
 * we allocate:
 *  (1) d = 3 color cultivation as the first level factory (pfail = 0.8%, perror = 1e-6)
 *  (2) 15:1 with (dx,dz,dm) = (25,11,11) as the second level factory (perror = 1e-12)
 * */
FACTORY_ALLOCATION throughput_aware_factory_allocation(size_t physical_qubit_budget,
                                                        FACTORY_SPECIFICATION l1_spec,
                                                        FACTORY_SPECIFICATION l2_spec);

/*
 * Estimates the magic state throughput of the given factory allocation
 * in magic states per second.
 * */
double estimate_throughput_of_allocation(const FACTORY_ALLOCATION&, bool first_level_is_cultivation);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace configuration
}  // namespace sim

#endif // SIM_CONFIGURATION_ALLOCATOR_h
