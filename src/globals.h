/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#ifndef GLOBALS_h
#define GLOBALS_h

#include <cstdint>
#include <iosfwd>
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
 * The value of `GL_USE_RPC_ISA` indicates the level.
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
extern int64_t GL_USE_RPC_ISA;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace sim
{

struct QUBIT
{
    qubit_type     qubit_id{-1};
    client_id_type client_id{-1};

    /*
     * This is the earliest cycle when the qubit is available
     * for some operation.
     * */
    cycle_type cycle_available{0};

    /*
     * These are used for calculating stats
     * */
    bool last_operation_was_memory_access{false};

    bool        operator==(const QUBIT&) const;
    std::string to_string() const;
};

std::ostream& operator<<(std::ostream&, const QUBIT&);

}  // namespace sim

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct _die {};

std::ostream& operator<<(std::ostream&, _die);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T> void print_stat_line(std::ostream&, std::string_view, T);
template <class T, class U> double mean(T, U);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#include "globals.tpp"

#endif  // GLOBALS_h
