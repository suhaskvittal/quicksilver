/*
    author: Suhas Vittal
    date:   6 September 2025
*/

#ifndef SIM_COMPUTE_REPLACEMENT_h
#define SIM_COMPUTE_REPLACEMENT_h

#include "sim/client.h"

namespace sim
{

class COMPUTE;

namespace compute
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct REPLACEMENT_POLICY_BASE
{
    /*
        Will always return a pointer to a client's qubit. If no
        replacement is possible, it will return a nullptr.
    */
    using output_type = CLIENT::qubit_info_type*;

    COMPUTE* cmp;

    REPLACEMENT_POLICY_BASE(COMPUTE*);
    /*
        `update_on_use` is called when a qubit inside of compute is used.
    */
    virtual void update_on_use(const CLIENT::qubit_info_type&) =0;
    /*
        `select_victim` is called when a qubit outside of compute is requested.
        The qubit that is requested is passed in as an argument.
    */
    virtual output_type select_victim(const CLIENT::qubit_info_type& requested) =0;
    /*
        Check is a qubit is a valid victim for replacement. This
        is a rudimentary set of checks. Descendants can override
        with additional checks if needed.

        The second function is an extended version that also takes in the requested qubit.
        It makes sure that the first argument does not belong to an instruction with `requested`,
        as evicting that qubit would cause a deadlock.
    */
    virtual bool is_valid_victim(const CLIENT::qubit_info_type&) const;
    bool is_valid_victim(const CLIENT::qubit_info_type&, const CLIENT::qubit_info_type& requested) const;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace compute
}   // namespace sim

#endif  // SIM_COMPUTE_REPLACEMENT_h