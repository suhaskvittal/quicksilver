/*
 *  author: SUhas Vittal
 *  date:   18 February 2026
 * */

#ifndef SIM_STATS_h
#define SIM_STATS_h

#include <cstdint>
#include <cstddef>

namespace sim
{
namespace stats
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

double ipc(uint64_t inst_count, cycle_type);
double ipdc(uint64_t inst_count, cycle_type, size_t code_distance);
double kips(uint64_t inst_count, cycle_type, double freq_khz);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace stats
}  // namespace sim

#endif // SIM_STATS_h
