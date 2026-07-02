/*
 *  author: SUhas Vittal
 *  date:   18 February 2026
 * */

#ifndef SIM_STATS_h
#define SIM_STATS_h

#include "globals.h"

namespace sim
{
namespace stats
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

inline double ipc(uint64_t i, cycle_type c) { return fpdiv(i,c); }
inline double ipdc(uint64_t i, cycle_type c, size_t d) { return fpdiv(i, fpdiv(c,d)); }
inline double kips(uint64_t i, cycle_type c, double freq_khz) { return 1e-3*fpdiv(i, c / (1e3*freq_khz)); }

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace stats
}  // namespace sim

#endif // SIM_STATS_h
