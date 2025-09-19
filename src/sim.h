/*
    author: Suhas Vittal
    date:   2025 August 18

    The only functions are utility functions for printing stats.
*/

#ifndef SIM_h
#define SIM_h

#include "sim/client.h"
#include "sim/clock.h"
#include "sim/compute.h"
#include "sim/factory.h"
#include "sim/memory.h"

#include <iostream>
#include <iomanip>
#include <string>
#include <random>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

extern uint64_t GL_CURRENT_TIME_NS;

// global pointer to the compute module
extern COMPUTE* GL_CMP;

// random number generators:
extern std::mt19937 GL_RNG;
extern std::uniform_real_distribution<double> FP_RAND;

// global flags:
extern bool     GL_PRINT_PROGRESS;
extern uint64_t GL_PRINT_PROGRESS_FREQ;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// implementation flags -- use this to setup your own designs
bool GL_IMPL_RZ_PREFETCH{false};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T> void
print_stat_line(std::ostream& out, std::string name, T value, bool indent=true)
{
    if (indent)
        name = "\t" + name;
    out << std::setw(52) << std::left << name << " : " << std::setw(12) << std::right << value << "\n";
}

void print_stats(std::ostream&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void arbitrate_event_execution();

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif // SIM_h