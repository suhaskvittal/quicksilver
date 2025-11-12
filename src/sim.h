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
#include "sim/epr_generator.h"
#include "sim/factory.h"
#include "sim/memory.h"

#include <chrono>
#include <iostream>
#include <iomanip>
#include <string>
#include <random>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

extern uint64_t GL_CURRENT_TIME_NS;

// simulation wall clock start time
extern std::chrono::steady_clock::time_point GL_SIM_WALL_START;

// global pointer to the compute module
extern COMPUTE* GL_CMP;

// global pointer to the shared EPR generator
extern EPR_GENERATOR* GL_EPR;

// random number generators:
extern std::mt19937 GL_RNG;
extern std::uniform_real_distribution<double> FP_RAND;

// global flags:
extern bool    GL_PRINT_PROGRESS;
extern int64_t GL_PRINT_PROGRESS_FREQ;

extern bool    GL_ELIDE_MSWAP_INSTRUCTIONS;
extern bool    GL_ELIDE_MPREFETCH_INSTRUCTIONS;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// implementation flags -- use this to setup your own designs
extern bool GL_IMPL_CACHEABLE_STORES;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T> void
print_stat_line(std::ostream& out, std::string name, T value, bool indent=true)
{
    if (indent)
        name = "   " + name;
    out << std::setw(52) << std::left << name << " : " << std::setw(12) << std::right << value << "\n";
}

void print_stats(std::ostream&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// returns the wall time elapsed since `GL_SIM_WALL_START` in format "<minutes>m <seconds>s <milliseconds>ms"
std::string sim_walltime();

// returns the number of seconds elapsed since `GL_SIM_WALL_START`
double sim_walltime_s();

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif // SIM_h
