/*
 *  author: Suhas Vittal
 *  date:   18 February 2026
 * */

#include "globals.h"
#include "sim/stats.h"

namespace sim
{
namespace stats
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
    
double
ipc(uint64_t i, cycle_type c)
{
    return mean(i,c);
}

double
ipdc(uint64_t i, cycle_type c, size_t d)
{
    return mean(i, mean(c,d));
}

double
kips(uint64_t i, cycle_type c, double freq_khz)
{
    return 1e-3*mean(i, c / (1e3*freq_khz));
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace stats
} // namespace sim
