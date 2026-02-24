/*
 *  author: Suhas Vittal
 *  date:   
 * */

#include "sim/configuration/resource_estimation.h"
#include "globals.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

namespace sim
{
namespace configuration
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

/*
 * Don't have good distance formula for bivariate bicycle code,
 * so we use this to check if the input `p` is something where
 * we know the block error rate.
 * */
void _verify_bivariate_bicycle_code_physical_error_rate(double);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

double
surface_code_logical_error_rate(size_t _d, double p)
{
    const double d = static_cast<double>(_d);
    return 0.1 * std::pow(100*p, 0.5*(d+1));
}

double
bivariate_bicycle_code_block_error_rate(size_t d, double p)
{
    _verify_bivariate_bicycle_code_physical_error_rate(p);

    if (d == 6)
        return 7e-5;
    else if (d == 12)
        return 2e-7;
    else if (d == 18)
        return 2e-12;
    else  // (d = 24)
        return 2e-17; // don't actually know, but fits the trend
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
surface_code_distance_for_target_logical_error_rate(double e, double p)
{
    const double ROUNDING_TOL{0.3};  // `0.3` is arbitrary, feel free to change to your favorite float

    double d = 2.0 * ( (std::log(e) - std::log(0.1)) / std::log(100*p) ) - 1.0;
    // need to round intelligently while avoiding floating point issues.
    double d_out = (d - std::floor(d) < ROUNDING_TOL) ? std::floor(d) : std::ceil(d);
    return std::max(static_cast<size_t>(d_out), size_t{2});
}

size_t
bivariate_bicycle_code_distance_for_target_block_error_rate(double e, double p)
{
    _verify_bivariate_bicycle_code_physical_error_rate(p);

    if (e >= 7e-5)
        return 6;
    else if (e >= 2e-7)
        return 12;
    else if (e >= 2e-12)
        return 18;
    else // (e >= 2e-17)
        return 24;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

void
_verify_bivariate_bicycle_code_physical_error_rate(double p)
{
    // change `ACCEPTABLE` if you add new values for a given physical error rate.
    const double ACCEPTABLE[] = {1e-3};
    const double TOL{1e-9};
    
    bool none_acceptable = std::none_of(std::begin(ACCEPTABLE), std::end(ACCEPTABLE),
                                    [p, TOL] (double x) { return std::abs(p-x) < TOL; });
    if (none_acceptable)
    {
        std::cerr << "_verify_bivariate_bicycle_code_physical_error_rate: unsupported physical error rate "
                    << p << ", acceptable error rates:";
        for (auto x : ACCEPTABLE)
            std::cerr << " " << x;
        std::cerr << _die{};
    }
}


} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace configuration
}  // namespace sim
