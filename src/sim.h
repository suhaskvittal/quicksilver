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

class COMPUTE_SUBSYSTEM;
class CLIENT;

/*
 * Utility function for printing out client stats:
 * */
void print_client_stats(std::ostream&, COMPUTE_SUBSYSTEM*, CLIENT*);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif // SIM_h
