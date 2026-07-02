/*
 *  author: Suhas Vittal
 *  date:   1 July 2026
 * */

#ifndef SIM_QUBIT_h
#define SIM_QUBIT_h

#include "globals.h"

#include <sstream>
#include <string>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct Qubit
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

    bool
    operator==(const Qubit& other) const
    {
        return (qubit_id == other.qubit_id) && (client_id == other.client_id);
    }
};

inline std::ostream&
operator<<(std::ostream& ostrm, const Qubit& q)
{
    ostrm << q.qubit_id << "(" << q.client_id << ")";
    return ostrm;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim

#endif
