/*
    author: Suhas Vittal
    date:   27 August 2025
*/

#include "clock.h"

#include <algorithm>
#include <cmath>

namespace sim
{


////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

double
compute_freq_khz(uint64_t t_sext_round_ns, size_t num_rounds_per_cycle)
{
    return 1.0e6 / static_cast<double>(t_sext_round_ns * num_rounds_per_cycle);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uint64_t
convert_cycles_to_ns(uint64_t t_cycles, double freq_khz)
{
    return static_cast<uint64_t>(std::ceil(t_cycles / freq_khz * 1e6));
}

uint64_t
convert_ns_to_cycles(uint64_t t_ns, double freq_khz)
{
    return static_cast<uint64_t>(std::ceil(t_ns * freq_khz * 1e-6));
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uint64_t
convert_cycles_between_frequencies(uint64_t t_cycles, double freq_khz_from, double freq_khz_to)
{
    return static_cast<uint64_t>( ceil(t_cycles * freq_khz_from / freq_khz_to) );
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim