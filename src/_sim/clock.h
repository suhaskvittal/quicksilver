/*
    author: Suhas Vittal
    date:   27 August 2025
*/

#ifndef SIM_CLOCK_h
#define SIM_CLOCK_h

#include <cstdint>
#include <cstddef>
#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

double compute_freq_khz(uint64_t t_sext_round_ns, size_t num_rounds_per_cycle);
uint64_t convert_cycles_to_ns(uint64_t t_cycles, double freq_khz);
uint64_t convert_ns_to_cycles(uint64_t t_ns, double freq_khz);
uint64_t convert_cycles_between_frequencies(uint64_t t_cycles, double freq_khz_from, double freq_khz_to);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif  // SIM_CLOCK_h
