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

    // this is a compute LER model for Libra (Cody Jones's surface code decoder) that I computed
    // manually
    // 0.045316423 * (p / 0.0067174571) ** (0.59777057 * d)
    const double A = 0.045316423,
                 PTH = 0.0067174571,
                 B = 0.59777057;
    return A * std::pow(p/PTH, B*d);
}

double
bivariate_bicycle_code_block_error_rate(size_t d, double p)
{
    _verify_bivariate_bicycle_code_physical_error_rate(p);

    if (d == 6)
        return 7e-4;
    else if (d == 12)
        return 2e-6;
    else if (d == 18)
        return 2e-10;
    else if (d == 24)
        return 2e-13; // don't actually know, but fits the trend
    else if (d == 30)
        return 2e-16;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
surface_code_distance_for_target_logical_error_rate(double e, double p)
{
    // `surface_code_logical_error_rate` is per-cycle (divides the closed-form `d`-cycle
    // estimate by `d`), so there's no closed-form inverse for `d` anymore -- `d` appears both
    // in the exponent and as a linear divisor. `surface_code_logical_error_rate(d, p)` is
    // monotonically decreasing in `d` for `p < 0.01`, so a linear search is sufficient (code
    // distances of interest are small).
    constexpr size_t D_MAX{201};

    size_t d = 2;
    while (d < D_MAX && surface_code_logical_error_rate(d, p) > e)
        d++;
    if (d == D_MAX)
    {
        std::cerr << "surface_code_distance_for_target_logical_error_rate: could not find a distance <= "
                    << D_MAX << " achieving target error rate " << e << " at p = " << p << _die{};
    }
    return d;
}

size_t
bivariate_bicycle_code_distance_for_target_block_error_rate(double e, double p)
{
    _verify_bivariate_bicycle_code_physical_error_rate(p);

    if (e >= 7e-5)
        return 6;
    else if (e >= 2e-7)
        return 12;
    else if (e >= 2e-18)
        return 18;
    else // (e >= 2e-25)
        return 24;
}

size_t
inner_surface_code_distance_for_target_logical_error_rate(double e, size_t d_outer, double p)
{
    const size_t d_inner_min = (e < 1e-3) ? size_t{3} : size_t{2};
    const size_t d_target = surface_code_distance_for_target_logical_error_rate(e, p);
    
    size_t d_inner = static_cast<size_t>( std::ceil(1.5 * fpdiv(d_target, d_outer)) );
    if (d_inner <= d_inner_min)
        return d_inner_min;
    if (d_inner % 2 == 0)
        d_inner++;
    return d_inner;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

void
_verify_bivariate_bicycle_code_physical_error_rate(double p)
{
    // change `ACCEPTABLE` if you add new values for a given physical error rate.
    constexpr double ACCEPTABLE[] = {1e-3};
    constexpr double TOL{1e-9};
    
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
