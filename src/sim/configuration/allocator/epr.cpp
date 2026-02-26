/*
 *  author: Suhas Vittal
 *  date:   24 February 2026
 * */

#include "sim/configuration/allocator/impl.h"
#include "sim/production/epr.h"

namespace sim
{

extern double GL_PHYSICAL_ERROR_RATE;

namespace configuration
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

PRODUCER_BASE* _alloc(ED_SPECIFICATION);
size_t         _physical_qubit_count(ED_SPECIFICATION);
double         _bandwidth(ED_SPECIFICATION, double);
double         _consumption_rate(ED_SPECIFICATION, double);

size_t _compute_inner_code_distance(size_t d_required, size_t d_outer, size_t d_inner_min);

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
    const size_t dm = surface_code_distance_for_target_logical_error_rate(s.output_error_rate, GL_PHYSICAL_ERROR_RATE);
    return new producer::ENT_DISTILLATION(freq_khz,
                                            s.output_error_rate,
                                            s.buffer_capacity,
                                            s.input_count,
                                            s.output_count,
                                            dm,
                                            s.input_count - s.output_count);
}

size_t
_physical_qubit_count(ED_SPECIFICATION s)
{
    // set `idmin` (inner code distance minimum) dynamically. For high output error rates, d = 3 makes 
    // no sense
    const size_t idmin = (s.output_error_rate < 1e-4) ? size_t{3} : size_t{2};

    const size_t d_base = surface_code_distance_for_target_logical_error_rate(s.output_error_rate, GL_PHYSICAL_ERROR_RATE);
    const size_t idx = _compute_inner_code_distance(d_base, s.dx, idmin);
    const size_t idz = _compute_inner_code_distance(d_base, s.dz, idmin);

    size_t p = surface_code_physical_qubit_count(idx, idz) * s.input_count;
    p *= 2;  // multiply by 2 to account for routing overheads.

    std::cout << "ED::_physical_qubit_count: [[ " << s.input_count 
                << ", " << s.output_count
                << ", dx=" << s.dx 
                << ", dz=" << s.dz 
                << " ]] uses inner codes with distance"
                << " dx = " << idx 
                << ", dz = " << idz
                << "\tphysical qubit overhead = " << p
                << "\n";

    // handle buffer overheads:
    p += (s.buffer_capacity - s.output_count) * surface_code_physical_qubit_count(d_base);
    return p;
}

double
_bandwidth(ED_SPECIFICATION s, double input_error_rate)
{
    if (input_error_rate < 0.0)
        input_error_rate = 10*GL_PHYSICAL_ERROR_RATE;

    const double freq_khz = compute_freq_khz(s.syndrome_extraction_round_time_ns);
    const size_t dm = surface_code_distance_for_target_logical_error_rate(s.output_error_rate, GL_PHYSICAL_ERROR_RATE);
    double rounds = static_cast<double>(dm * (s.input_count-s.output_count));
    
    if (input_error_rate >= 0.0)
    {
        const size_t dm_in = surface_code_distance_for_target_logical_error_rate(input_error_rate, GL_PHYSICAL_ERROR_RATE);
        rounds += dm_in * s.input_count;
    }

    double probability_of_success = std::pow(1 - input_error_rate, s.input_count);
    double rounds_until_success = rounds / probability_of_success;
    return (1e3 * freq_khz * s.output_count) / rounds_until_success;
}

double
_consumption_rate(ED_SPECIFICATION s, double input_error_rate)
{
    const double freq_khz = compute_freq_khz(s.syndrome_extraction_round_time_ns);
    const size_t dm = surface_code_distance_for_target_logical_error_rate(s.output_error_rate, GL_PHYSICAL_ERROR_RATE);
    double rounds = static_cast<double>(dm * (s.input_count-s.output_count));
    
    if (input_error_rate >= 0.0)
    {
        const size_t dm_in = surface_code_distance_for_target_logical_error_rate(input_error_rate, GL_PHYSICAL_ERROR_RATE);
        rounds += dm_in * s.input_count;
    }

    return (1e3 * freq_khz * s.input_count) / rounds;
}

size_t
_compute_inner_code_distance(size_t d_required, size_t d_outer, size_t d_inner_min)
{
    size_t d_inner = static_cast<size_t>( std::ceil(mean(d_required, d_outer)) );
    if (d_inner <= d_inner_min)
        return d_inner_min;
    return d_inner;
}

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace configuration
}  // namespace sim
