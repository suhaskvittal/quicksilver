/*
    author: Suhas Vittal
    date:   2025 August 18

    The only functions are utility functions for printing stats.
*/

#ifndef SIM_h
#define SIM_h

#include "sim/clock.h"
#include "sim/compute.h"
#include "sim/client.h"
#include "sim/routing.h"
#include "sim/magic_state.h"
#include "sim/memory.h"

#include <iostream>
#include <iomanip>
#include <string>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T> void
print_stat_line(std::ostream& out, std::string name, T value, bool indent=true)
{
    if (indent)
        name = "\t" + name;
    out << std::setw(52) << std::left << name << " : " << std::setw(12) << std::right << value << "\n";
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void print_stats(std::ostream& out, sim::COMPUTE*, std::vector<sim::T_FACTORY*>, std::vector<sim::MEMORY_MODULE*>);
void print_progress(std::ostream& out, COMPUTE*, uint64_t tick, uint64_t dot_freq, uint64_t newline_freq);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif // SIM_h