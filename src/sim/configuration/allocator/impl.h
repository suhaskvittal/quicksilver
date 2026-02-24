/*
 *  author: Suhas Vittal
 *  date:   24 February 2026
 * */

#ifndef SIM_CONFIGURATION_ALLOCATION_IMPL_h
#define SIM_CONFIGURATION_ALLOCATION_IMPL_h

#include "sim/configuration/allocator.h"

namespace sim
{
namespace configuration
{

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

struct ED_SPECIFICATION /* entanglement distillation */
{
    /* Defaults are for distillation via a [[2, 1, 2]]_X code */
    uint64_t syndrome_extraction_round_time_ns{1200};
    size_t   buffer_capacity{1};
    double   output_error_rate{1e-3};
    size_t   input_count{2};
    size_t   output_count{1};

    /*
     * `dx` and `dz` are parameters of the code used for distillation, not the
     * underlying logical qubits
     * */
    size_t dx{2};
    size_t dz{1};
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ALLOCATION allocate_magic_state_factories(size_t budget, std::vector<FACTORY_SPECIFICATION>);
ALLOCATION allocate_entanglement_distillation_units(size_t budget, std::vector<ED_SPECIFICATION>);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace configuration
}  // namespace sim

#endif  // SIM_CONFIGURATION_ALLOCATION_IMPL_h
