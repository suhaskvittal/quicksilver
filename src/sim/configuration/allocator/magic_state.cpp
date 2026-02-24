/*
 *  author: Suhas Vittal
 *  date:   24 February 2026
 * */

#include "sim/configuration/allocator/impl.h"
#include "sim/production/magic_state.h"

#include <cassert>

namespace sim
{

extern double GL_PHYSICAL_ERROR_RATE;

namespace configuration
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Helper functions. These are provided as the functions
 * for the template `throughput_aware_allocation()` function.
 * */

namespace
{

PRODUCER_BASE* _alloc(FACTORY_SPECIFICATION);
size_t         _physical_qubit_count(FACTORY_SPECIFICATION);
double         _bandwidth(FACTORY_SPECIFICATION, double);
double         _consumption_rate(FACTORY_SPECIFICATION);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ALLOCATION
allocate_magic_state_factories(size_t b, std::vector<FACTORY_SPECIFICATION> specs)
{
    return throughput_aware_allocation(b, specs, _alloc, _physical_qubit_count, _bandwidth, _consumption_rate);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Helper functions start here:
 * */

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

PRODUCER_BASE*
_alloc(FACTORY_SPECIFICATION s)
{
    PRODUCER_BASE* f;
        const double freq_khz = compute_freq_khz(s.syndrome_extraction_round_time_ns);
    if (s.is_cultivation)
    {
        f = new producer::T_CULTIVATION(freq_khz,
                                        s.output_error_rate,
                                        s.buffer_capacity,
                                        s.probability_of_success,
                                        s.rounds);
    }
    else
    {
        f = new producer::T_DISTILLATION(freq_khz,
                                         s.output_error_rate,
                                         s.buffer_capacity,
                                         s.input_count,
                                         s.output_count,
                                         s.dm,
                                         s.rotations);
    }
    return f;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
_physical_qubit_count(FACTORY_SPECIFICATION s)
{
    size_t p = s.is_cultivation
                ? magic_state_cultivation_physical_qubit_count(s.escape_distance)
                : magic_state_distillation_physical_qubit_count(s.input_count, s.output_count, s.dx, s.dz);
    // handle buffer overheads:
    size_t output_count = s.is_cultivation ? 1 : s.output_count;
    size_t d_buffer = surface_code_distance_for_target_logical_error_rate(s.output_error_rate, GL_PHYSICAL_ERROR_RATE);
    p += (s.buffer_capacity-output_count) * surface_code_physical_qubit_count(d_buffer);
    return p;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

double
_bandwidth(FACTORY_SPECIFICATION s, double)
{
    double bw;

    const double freq_khz = compute_freq_khz(s.syndrome_extraction_round_time_ns);
    if (s.is_cultivation)
    {
        double mean_tries_until_success = 1.0/s.probability_of_success;
        // on average, we will fail the in the middle of the cultivation procedure (`s.rounds * 0.5`)
        double failure_rounds = mean_tries_until_success * s.rounds * 0.5;
        bw = (1e3*freq_khz) / (s.rounds + failure_rounds);
    }
    else
    {
        // there are `s.dm` rounds per step
        double step_count = static_cast<double>(s.rotations+1);
        bw = (1e3 * freq_khz * s.output_count) / (s.dm * step_count);
    }
    return bw;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

double
_consumption_rate(FACTORY_SPECIFICATION s)
{
    assert(!s.is_cultivation);

    const double freq_khz = compute_freq_khz(s.syndrome_extraction_round_time_ns);
    double states_consumed = static_cast<double>(s.input_count + s.rotations);
    double step_count = static_cast<double>(s.rotations+1);
    return (1e3 * freq_khz * states_consumed) / (s.dm * step_count);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace configuration
}  // namespace sim
