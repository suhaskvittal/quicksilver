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

CLOCKABLE::CLOCKABLE(double freq_khz)
    :freq_khz_(freq_khz)
{}

void
CLOCKABLE::tick()
{
    if (leap_ < 1e-10)
    {
        operate();    
        cycle_++;
        leap_ += clk_scale_;
    }
    else
    {
        leap_ -= 1.0;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
setup_clk_scale_for_group(std::vector<CLOCKABLE*> clockables)
{
    auto max_it = std::max_element(clockables.begin(), clockables.end(),
                                        [] (auto* a, auto* b) { return a->freq_khz_ < b->freq_khz_; });
    double max_freq = (*max_it)->freq_khz_;

    for (auto* c : clockables)
        c->clk_scale_ = (max_freq/c->freq_khz_) - 1.0;
}

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
convert_cycles_between_frequencies(uint64_t t_cycles, double freq_khz_from, double freq_khz_to)
{
    return static_cast<uint64_t>( ceil(t_cycles * freq_khz_from / freq_khz_to) );
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim