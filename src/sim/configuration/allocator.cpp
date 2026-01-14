/*
 *  author: Suhas Vittal
 *  date:   14 January 2026
 * */

#include "sim/configuration/allocator.h"
#include "sim/factory.h"
#include "sim/operable.h"

#include <cassert>
#include <cmath>

namespace sim
{
namespace configuration
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Helper functions and constants:
 * */

namespace
{

constexpr size_t CULTIVATION_D{3};
constexpr size_t CULTIVATION_ROUND_LENGTH{25};

constexpr std::string_view DISTILLATION_TYPE{"15:1"};
constexpr size_t DISTILLATION_DX{25},
                 DISTILLATION_DZ{11},
                 DISTILLATION_DM{11};

constexpr size_t CULTIVATION_PHYSICAL_QUBIT_COUNT = magic_state_cultivation_physical_qubit_count(CULTIVATION_D);
constexpr size_t DISTILLATION_PHYSICAL_QUBIT_COUNT = magic_state_distillation_physical_qubit_count(
                                                                            DISTILLATION_TYPE, 
                                                                            DISTILLATION_DX, 
                                                                            DISTILLATION_DZ);

double _estimate_throughput_first_level(const std::vector<T_FACTORY_BASE*>&);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

FACTORY_ALLOCATION
throughput_aware_factory_allocation(size_t physical_qubit_budget,
                                    uint64_t syndrome_extraction_round_time_ns)
{
    const double cultivation_freq_khz = compute_freq_khz(syndrome_extraction_round_time_ns * CULTIVATION_ROUND_LENGTH);
    const double distillation_freq_khz = compute_freq_khz(syndrome_extraction_round_time_ns * DISTILLATION_DM);

    FACTORY_ALLOCATION factory_alloc;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

double
estimate_throughput_of_allocation(const FACTORY_ALLOCATION& factory_alloc)
{
    /* 1. compute first level throughput, since this affects second level throughput */

    // `first_level_tp` is in `magic states per second`
    const double first_level_tp = _estimate_throughput_first_level(factory_alloc.first_level);
    if (second_level.empty())
        return first_level_tp;


    /* 2. compute the consumption rate (per cycle) of the second level */
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

double
_estimate_throughput_first_level(const std::vector<T_FACTORY_BASE*>& fact)
{
    double tp = std::transform_reduce(fact.begin(), fact.end(), double{0.0},
                                        std::plus<double>{},
                                        [] (T_FACTORY_BASE* _f)
                                        {
                                            // cast to `T_CULTIVATION`
                                            T_CULTIVATION* f = dynamic_cast<T_CULTIVATION*>(_f);
                                            assert(f != nullptr);
                                            return (1e3 / f->freq_khz) * probability_of_success;
                                        });
    return tp;
}

}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace configuration
}  // namespace sim
