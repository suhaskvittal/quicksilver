/*
 *  author: Suhas Vittal
 *  date:   14 January 2026
 * */

#ifndef SIM_CONFIGURATION_ALLOCATOR_h
#define SIM_CONFIGURATION_ALLOCATOR_h

#include "sim/factory.h"

namespace sim
{
namespace configuration
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct FACTORY_ALLOCATION
{
    std::vector<T_FACTORY_BASE*> first_level;
    std::vector<T_FACTORY_BASE*> second_level;

    FACTORY_ALLOCATION() =default;
    FACTORY_ALLOCATION(const FACTORY_ALLOCATION&) =default;
};

/*
 * Allocates a factory within a given physical qubit budget.
 *
 * Since we target an error rate of 1e-12 per magic state,
 * we allocate:
 *  (1) d = 3 color cultivation as the first level factory (pfail = 0.8%, perror = 1e-6)
 *  (2) 15:1 with (dx,dz,dm) = (25,11,11) as the second level factory (perror = 1e-12)
 * */

FACTORY_ALLOCATION throughput_aware_factory_allocation(size_t physical_qubit_budget,
                                                        uint64_t syndrome_extraction_round_time_ns);

/*
 * Estimates the magic state throughput of the given factory allocation
 * in magic states per second.
 * */
double estimate_throughput_of_allocation(const FACTORY_ALLOCATION&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace configuration
}  // namespace sim

#endif // SIM_CONFIGURATION_ALLOCATOR_h
