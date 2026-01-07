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
template <class T> double mean(T, T);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#include "globals.tpp"

#endif  // GLOBALS_h
