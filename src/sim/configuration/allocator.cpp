/*
 *  author: Suhas Vittal
 *  date:   14 January 2026
 * */

#include "sim/configuration/allocator.h"
#include "sim/configuration/resource_estimation.h"
#include "sim/operable.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numeric>

namespace sim
{
namespace configuration
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Helper functions:
 * */

namespace
{

T_FACTORY_BASE* _alloc_factory(FACTORY_SPECIFICATION);

size_t _throughput_aware_alloc_step(FACTORY_ALLOCATION&,
                                    size_t remaining_budget, 
                                    FACTORY_SPECIFICATION, 
                                    FACTORY_SPECIFICATION);

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
                                    FACTORY_SPECIFICATION l1_spec,
                                    FACTORY_SPECIFICATION l2_spec)
{
    assert(!l2_spec.is_cultivation);

    FACTORY_ALLOCATION factory_alloc;
    size_t remaining_budget{physical_qubit_budget};
    while (remaining_budget > 0)
        remaining_budget = _throughput_aware_alloc_step(factory_alloc, remaining_budget, l1_spec, l2_spec);

    for (auto* f : factory_alloc.second_level)
        f->previous_level = factory_alloc.first_level;
    return factory_alloc;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

double
estimate_throughput_of_allocation(const FACTORY_ALLOCATION& factory_alloc, bool first_level_is_cultivation)
{
    const double first_level_throughput = first_level_is_cultivation 
                                            ? _estimate_throughput_cultivation(factory_alloc.first_level)
                                            : _estimate_throughput_distillation(factory_alloc.first_level);
    if (factory_alloc.second_level.empty())
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

T_FACTORY_BASE*
_alloc_factory(FACTORY_SPECIFICATION spec)
{
    T_FACTORY_BASE* f;

    const double freq_khz = spec.is_cultivation 
                                ? compute_freq_khz(spec.syndrome_extraction_round_time_ns * spec.round_length)
                                : compute_freq_khz(spec.syndrome_extraction_round_time_ns * spec.dm);
    if (spec.is_cultivation)
    {
        f = new T_CULTIVATION(freq_khz, spec.output_error_rate, spec.buffer_capacity, spec.probability_of_success);
    }
    else
    {
        f = new T_DISTILLATION(freq_khz, spec.output_error_rate, spec.buffer_capacity, 
                                spec.input_count, spec.output_count, spec.rotations);
    }
    return f;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
_throughput_aware_alloc_step(FACTORY_ALLOCATION& alloc, size_t b, FACTORY_SPECIFICATION s1, FACTORY_SPECIFICATION s2)
{
    /* 1. Declare constants for physical qubit overheads and check if `b` is sufficiently high */

    const size_t pq_count_first_level = s1.is_cultivation
                                        ? magic_state_cultivation_physical_qubit_count(s1.escape_distance)
                                        : magic_state_distillation_physical_qubit_count(s1.input_count,
                                                                                        s1.output_count,
                                                                                        s1.dx, 
                                                                                        s1.dz);
    const size_t pq_count_second_level = magic_state_distillation_physical_qubit_count(s2.input_count, 
                                                                                        s2.output_count,
                                                                                        s2.dx, 
                                                                                        s2.dz);
    const size_t min_pq_count_to_allocate = pq_count_first_level + pq_count_second_level;

    // exit early and return 0 (to indicate loop to stop) if there are not enough qubits for allocation
    if (b < min_pq_count_to_allocate)
    {
        if (alloc.second_level.empty())
        {
            std::cerr << "_throughput_aware_alloc_step: cannot allocate any 2nd level distillation factories"
                        << "\n\tbudget = " << b
                        << "\n\trequired for one first level = " << pq_count_first_level
                        << "\n\trequired for one second level = " << pq_count_second_level
                        << "\n\ttotal required = " << min_pq_count_to_allocate
                        << _die{};
        }
        return 0;
    }

    /* 1. Allocate a second level factory */

    const double initial_throughput = estimate_throughput_of_allocation(alloc, s1.is_cultivation);
    alloc.second_level.push_back(_alloc_factory(s2));
    b -= pq_count_second_level;

    /* 2. Add first level factories while within budget */

    size_t num_added{0};
    double curr_tp{initial_throughput},
           prev_tp;
    do
    {
        prev_tp = curr_tp;
        alloc.first_level.push_back(_alloc_factory(s1));
        b -= pq_count_first_level;
        num_added++;
        curr_tp = estimate_throughput_of_allocation(alloc, s1.is_cultivation);
    }
    while (curr_tp > prev_tp && b >= pq_count_first_level);

    if (curr_tp == prev_tp)
    {
        // if `curr_tp == prev_tp`, then we should remove the last first level factory
        // since it adds no benefit
        delete alloc.first_level.back();
        alloc.first_level.pop_back();
        b += pq_count_first_level;
        num_added--;
        curr_tp = estimate_throughput_of_allocation(alloc, s1.is_cultivation);
    }
    
    /* 3 . If `curr_tp < initial_throughput`, undo all additions */

    if (curr_tp < initial_throughput)
    {
        delete alloc.second_level.back();
        alloc.second_level.pop_back();
        while (num_added--)
        {
            delete alloc.first_level.back();
            alloc.first_level.pop_back();
        }
        // set `b` to `0` to indicate we are done
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
                                            auto* f = static_cast<T_CULTIVATION*>(_f);
                                            return (1e3 * f->freq_khz) * f->probability_of_success;
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
                                            auto* f = static_cast<T_DISTILLATION*>(_f);
                                            const size_t num_steps = 1 + f->num_rotation_steps;
                                            const double eff_freq_khz = f->freq_khz / static_cast<double>(num_steps);
                                            return (1e3 * eff_freq_khz) * f->output_count;
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
                                        auto* f = static_cast<T_DISTILLATION*>(_f);
                                        const size_t num_steps = 1 + f->num_rotation_steps;
                                        const size_t states_consumed = f->initial_input_count + f->num_rotation_steps;

                                        const double eff_freq_khz = f->freq_khz / static_cast<double>(num_steps);
                                        return (1e3 * eff_freq_khz) * states_consumed;
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
