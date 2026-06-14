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

class DRIVER;
class COMPUTE_SUBSYSTEM;
class CLIENT;
class PRODUCER_BASE;

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

/*
 * Physical error rate -- mostly important for resource estimation.
 * */
extern double GL_PHYSICAL_ERROR_RATE;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

extern bool GL_OPERATE_AS_NEUTRAL_ATOM;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Reaction time in terms of compute cycles.
 * */
extern int64_t GL_REACTION_TIME;

/*
 * Reaction-limited T teleportation
 *  degree = max number of T gates to do in parallel
 * */
extern int64_t GL_RLTP_DEGREE;

/*
 * RDR parameters:
 * */
extern bool GL_RDR_ENABLED;
extern int64_t GL_RDR_CAPACITY;
extern int64_t GL_RDR_START_LAYER;
extern int64_t GL_RDR_LOOKAHEAD_DEPTH;
extern int64_t GL_RDR_INST_DELTA_LIMIT;
extern int64_t GL_RDR_DEGREE;
extern int64_t GL_RDR_COMPLETION_BUFFER_CAPACITY;
extern bool GL_RDR_FIXED_LOOKAHEAD;
extern double GL_RDR_COST_SCALE;

constexpr client_id_type RDR_CLIENT_ID{-47};

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

void print_sim_stats(std::ostream&, DRIVER*);
void print_stats_for_factories(std::ostream&, std::string_view header, std::vector<PRODUCER_BASE*>);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif // SIM_h
