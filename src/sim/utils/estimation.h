/*
 *  author: Suhas Vittal
 *  date:   8 October 2025
 * */

#ifndef SIM_UTILS_ESTIMATION_h
#define SIM_UTILS_ESTIMATION_h

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sim
{
namespace est
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

inline size_t
sc_phys_qubit_count(size_t d)
{
    return 2*d*d-1;
}

inline size_t
sc_phys_qubit_count(size_t dx, size_t dz)
{
    return 2*dx*dz-1;
}

inline size_t
bb_phys_qubit_count(size_t d)
{
    return 2*72*(d/6);
}

inline size_t
fact_logical_qubit_count(std::string which)
{
    // this includes the ancillary space required to perform pauli-product rotations
    if (which == "15to1")
        return 9;
    else if (which == "20to4")
        return 12;
    else
        throw std::runtime_error("fact_logical_qubit_count: unknown logical qubit count for " + which);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

const double PHYS_ERROR{1e-3};

inline double
sc_logical_error_rate(size_t d)
{
    return 0.1 * pow(100*PHYS_ERROR, 0.5*(d+1));
}

inline size_t
sc_distance_for_target_logical_error_rate(double e)
{
    double d = 2.0 * ((log(e) - log(0.1)) / log(100*PHYS_ERROR)) - 1.0;

    // need to intelligently round while avoiding floating point issues
    size_t d_fl = static_cast<size_t>(floor(d)),
           d_ce = static_cast<size_t>(ceil(d));

    // 0.1 is insignificant enough that we can round down.
    if (d - d_fl < 0.1)
        return d_fl;
    else
        return d_ce;
}

inline double
mem_bb_logical_error_rate(size_t d)
{
    if (d == 6)
        return 7e-5;
    else if (d == 12)
        return 2e-7;
    else if (d == 18)
        return 2e-12;
    else  // (d = 24)
        return 2e-17;  // don't actually know this one
}

inline size_t
mem_bb_distance_for_target_logical_error_rate(double e)
{
    if (e >= 7e-5)
        return 6;
    else if (e >= 2e-7)
        return 12;
    else if (e >= 2e-12)
        return 18;
    else  // (e = 2e-17)
        return 24;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace est
}   // namespace sim

#endif
