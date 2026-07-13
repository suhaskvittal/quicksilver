/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#ifndef GLOBALS_h
#define GLOBALS_h

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

using qubit_type =     int64_t;
using client_id_type = int8_t;
using cycle_type =     uint64_t;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Modifies the instruction representation for RPC (Rotation Pre-Computation)
 * The value of `GL_USE_RDR_ISA` indicates the level.
 *
 * Level 0 = do not use
 *       1 = only add 2*phi to the instruction representation
 *       2 = add both 2*phi and 4*phi to the instruction representation
 *       (etc.)
 *
 * So higher levels increase compile times and binary sizes.
 *
 * This affects `instruction.h` and `compile/program.h`.
 * If the level is set to 0 (default), then corrective rotations
 * are not used.
 * */
extern int64_t GL_USE_RDR_ISA;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct _die {};

std::ostream& operator<<(std::ostream&, _die);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T> void 
print_stat_line(std::ostream& ostrm, std::string_view name, T value)
{
    ostrm << std::setw(64) << std::left << name;
    if constexpr (std::is_floating_point<T>::value)
    {
        if (std::abs(value) < 1e-2)
            ostrm << std::setw(12) << std::right << std::scientific << std::setprecision(5) << value;
        else
            ostrm << std::setw(12) << std::right << std::fixed << std::setprecision(3) << value;
    }
    else
    {
        ostrm << std::setw(12) << std::right << value;
    }
    ostrm << "\n";
}

template <class T, class U> constexpr double
fpdiv(T x, U y) 
{ 
    return static_cast<double>(x) / static_cast<double>(y);
}

template <class T> constexpr T 
sqr(T x) 
{ 
    return x*x;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif  // GLOBALS_h
