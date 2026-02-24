/*
 *  author: Suhas Vittal
 *  date:   24 February 2026
 * */

#include "sim/configuration/allocator/impl.h"
#include "sim/production/epr.h"

namespace sim
{
namespace configuration
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

PRODUCER_BASE* _alloc(ED_SPECIFICATION);
size_t         _physical_qubit_count(ED_SPECIFICATION);
double         _bandwidth(ED_SPECIFICATION);
double         _consumption_rate(ED_SPECIFICATION);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ALLOCATION
allocate_entanglement_distillation_units(size_t b, std::vector<ED_SPECIFICATION> specs)
{
    return throughput_aware_allocation(b, specs, _alloc, _physical_qubit_count, _bandwidth, _consumption_rate);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

PRODUCER_BASE*
_alloc(ED_SPECIFICATION s)
{
    const double freq_khz = compute_freq_khz(s.syndrome_extraction_round_time_ns);
    const size_t dm = surface_code_distance_for_target_logical_error_rate(s.output_error_rate);
    return new ENT_DISTILLATION(freq_khz,
                                s.output_error_rate,
                                dm,
                                s.input_count,
                                s.output_count,
                                s.input_count - s.output_count);
}

size_t
_physical_qubit_count(ED_SPECIFICATION s)
{
    const size_t d_base = surface_code_distance_for_target_logical_error_rate(s.output_error_rate);
    const size_t idx = d_base / s.dx;
    const size_t idz = d_base / s.dz;
    size_t p = surface_code_physical_qubit_count(idx, idz) * s.input_count;

    // handle buffer overheads:
    p += (s.buffer_capacity - s.output_count) * surface_code_physical_qubit_count(d_base);
    return p;
}

double
_bandwidth(ED_SPECIFICATION s)
{
    const double freq_khz = compute_freq_khz(s.syndrome_extraction_round_time_ns);
    const size_t dm = surface_code_distance_for_target_logical_error_rate(s.output_error_rate);
    double rounds = static_cast<double>(dm * (s.input_count-s.output_count));
    return (1e3 * freq_khz * s.output_count) / rounds;
}

double
_consumption_rate(ED_SPECIFICATION s)
{
    const double freq_khz = compute_freq_khz(s.syndrome_extraction_round_time_ns);
    const size_t dm = surface_code_distance_for_target_logical_error_rate(s.output_error_rate);
    double rounds = static_cast<double>(dm * (s.input_count-s.output_count));
    return (1e3 * freq_khz * s.input_count) / rounds;
}

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace configuration
}  // namespace sim
