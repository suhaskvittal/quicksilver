/*
    author: Suhas Vittal
    date:   2025 August 18

    The only functions are utility functions for printing stats.
*/

#ifndef SIM_h
#define SIM_h

#include "globals.h"

#include <chrono>
#include <iostream>
#include <iomanip>
#include <string>
#include <random>

namespace sim
{

class COMPUTE_SUBSYSTEM;
class CLIENT;
class T_FACTORY_BASE;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Simulation wall clock start time
 * */
extern std::chrono::steady_clock::time_point GL_SIM_WALL_START;

/*
 * Global random number generator
 * */
extern std::mt19937_64 GL_RNG;

/*
 * Number of compute cycles per progress print
 * */
extern int64_t GL_PRINT_PROGRESS_FREQUENCY;

/*
 * Maximum number of simulation cycles with no progress
 * before declaring deadlock and killing the program 
 * (see `operable.h` and `operable.cpp`)
 * */
extern int64_t GL_MAX_CYCLES_WITH_NO_PROGRESS;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Assume auto-correction is used for T gates.
 * */
extern bool GL_T_GATE_DO_AUTOCORRECT;

/*
 * Enable T gate teleportation via GHZ gates (default 0 -- T gates are directly applied on
 * target qubit).
 * */
extern int64_t GL_T_GATE_TELEPORTATION_MAX;

/*
 * RPC parameters:
 *  `GL_RPC_ALWAYS_USE_TELEPORTATION`: parallel T gate teleportation when rotation is non-critical
 *  `GL_RPC_ALWAYS_RUNAHEAD`: trigger runahead also when rotation suceeds
 *  `GL_RPC_INST_DELTA_LIMIT`: inst delta limit for runahead (bound on runahead and triggering inst-number delta)
 * */
extern bool GL_RPC_ALWAYS_USE_TELEPORTATION;
extern bool GL_RPC_ALWAYS_RUNAHEAD;
extern int64_t GL_RPC_INST_DELTA_LIMIT;
extern int64_t GL_RPC_DEGREE;

/*
 * These variables are just for debugging/speed-of-light analysis:
 * */
extern bool GL_ELIDE_CLIFFORDS;
extern bool GL_ZERO_LATENCY_T_GATES;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Implementation flags -- use this to setup your own designs
 * */

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// returns the wall time elapsed since `GL_SIM_WALL_START` in format "<minutes>m <seconds>s <milliseconds>ms"
std::string walltime();

// returns the number of seconds elapsed since `GL_SIM_WALL_START`
double walltime_s();

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Stat printing utilities:
 * */

void print_compute_subsystem_stats(std::ostream&, COMPUTE_SUBSYSTEM*);
void print_stats_for_factories(std::ostream&, std::string_view header, std::vector<T_FACTORY_BASE*>);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif // SIM_h
