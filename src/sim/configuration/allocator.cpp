/*
 *  author: Suhas Vittal
 *  date:   14 January 2026
 * */

#include "sim/configuration/allocator.h"
#include "sim/operable.h"

#include <algorithm>
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

size_t _throughput_aware_alloc_step(FACTORY_ALOCATION&, size_t remaining_budget);

void _alloc_first_level_factory();
void _alloc_second_level_factory();

/*
 * Helper functions for estimating throughput and consumption rate.
 * Both metrics are given in magic states per second.
 * */
double _estimate_throughput_cultivation(const std::vector<T_FACTORY_BASE*>&);
double _estimate_throughput_distillation(const std::vector<T_FACTORY_BASE*>&);
double _estimate_consumption_rate(const std::vector<T_FACTORY_BASE*>&);

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

    size_t remaining_budget{physical_qubit_budget};
    while (remaining_budget >= (CULTIVATION_PHYSICAL_QUBIT_COUNT+DISTILLATION_PHYSICAL_QUBIT_COUNT))
        remaining_budget = _throughput_aware_alloc_step(factory_alloc, remaining_budget);

    if (factory_alloc.second_level.empty())
    {
        std::cerr << "throughput_aware_factory_allocation: cannot allocate any 2nd level distillation factories"
                    << "\n\tbudget = " << physical_qubit_budget
                    << "\n\trequired for one first level = " << CULTIVATION_PHYSICAL_QUBIT_COUNT
                    << "\n\trequired for one second level = " << DISTILLATION_PHYSICAL_QUBIT_COUNT
                    << "\n\ttotal required = " << (CULTIVATION_PHYSICAL_QUBIT_COUNT+DISTILLATION_PHYSICAL_QUBIT_COUNT)
                    << _die{};
    }

    return factory_alloc;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

double
estimate_throughput_of_allocation(const FACTORY_ALLOCATION& factory_alloc)
{
    const double first_level_throughput = _estimate_throughput_cultivation(factory_alloc.first_level);
    if (second_level.empty())
        return first_level_throughput;

    const double second_level_consumption_rate = _estimate_consumption_rate(factory_alloc.second_level);
    const double ratio = std::min(1.0, first_level_throughput/second_level_consumption_rate);

    double tp = _estimate_throughput_distillation(factory_alloc.second_level) * ratio;
    return tp;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
_throughput_aware_alloc_step(FACTORY_ALLOCATION& alloc, size_t b)
{
    assert(b >= CULTIVATION_PHYSICAL_QUBIT_COUNT+DISTILLATION_PHYSICAL_QUBIT_COUNT);
    
    const double initial_throughput = estimate_throughput_of_allocation(alloc);

    /* 1. Allocate a second level factory */
    alloc.second_level.push_back(_alloc_second_level_factory());
    b -= DISTILLATION_PHYSICAL_QUBIT_COUNT;

    /* 2. Add first level factories while within budget. 
     *    Stop when `curr_tp <= prev_tp` or `b < CULTIVATION_PHYSICAL_QUBIT_COUNT` */

    size_t num_added{0};
    double curr_tp = estimate_throughput_of_allocation(alloc), 
           prev_tp;
    do
    {
        prev_tp = curr_tp;
        alloc.first_level.push_back(_alloc_first_level_factory());
        b -= CULTIVATION_PHYSICAL_QUBIT_COUNT;
        num_added++;
        curr_tp = estimate_throughput_of_allocation(alloc);
    }
    while (curr_tp > prev_tp && b >= CULTIVATION_PHYSICAL_QUBIT_COUNT);

    if (curr_tp == prev_tp)
    {
        // if `curr_tp == prev_tp`, then we should remove the last first level factory
        // since it adds no benefit
        delete alloc.first_level.back();
        alloc.first_level.pop_back();
        b += CULTIVATION_PHYSICAL_QUBIT_COUNT;
        num_added--;
        curr_tp = estimate_throughput_of_allocation(alloc);
    }
    
    /* If `curr_tp < initial_throughput`, undo all additions */
    if (curr_tp < initial_throughput)
    {
        delete alloc.second_level.back();
        alloc.second_level.pop_back();
        while (num_added--)
        {
            delete alloc.first_level.back();
            alloc.first_level.pop_back();
        }
        // set `b` to `0` since it is clear we are done.
        b = 0;
    }

    return b;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

double
_estimate_throughput_cultivation(const std::vector<T_FACTORY_BASE*>& fact)
{
    double tp = std::transform_reduce(fact.begin(), fact.end(), double{0.0},
                                        std::plus<double>{},
                                        [] (T_FACTORY_BASE* _f)
                                        {
                                            T_CULTIVATION* f = static_cast<T_CULTIVATION*>(_f);
                                            return (1e3 / f->freq_khz) * f->probability_of_success;
                                        });
    return tp;
}

double
_estimate_throughput_distillation(const std::vector<T_FACTORY_BASE*>& fact)
{
    double tp = std::transform_reduce(fact.begin(), fact.end(), double{0.0},
                                        std::plus<double>{},
                                        [] (T_FACTORY_BASE* _f)
                                        {
                                            T_CULTIVATION* f = static_cast<T_DISTILLATION*>(_f);
                                            const size_t num_steps = 1 + f->num_rotation_steps;
                                            const double eff_freq_khz = f->freq_khz / static_cast<double>(num_steps);
                                            return (1e3 / eff_freq_khz) * f->output_count;
                                        });
    return tp;
}

double
_estimate_consumption_rate(const std::vector<T_FACTORY_BASE*>& fact)
{
    double r = std::transform_reduce(fact.begin(), fact.end(), double{0.0},
                                    std::plus<double>{},
                                    [] (T_FACTORY_BASE* _f)
                                    {
                                        T_DISTILLATION* f = static_cast<T_DISTILLATION*>(_f);
                                        const size_t num_steps = 1 + f->num_rotation_steps;
                                        const size_t states_consumed = f->initial_input_count + f->num_rotation_steps;

                                        const double eff_freq_khz = f->freq_khz / static_cast<double>(num_steps);
                                        return (1e3 / eff_freq_khz) * states_consumed;
                                    });
    return r;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace configuration
}  // namespace sim
