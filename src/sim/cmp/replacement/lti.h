/*
    author: Suhas Vittal
    date:   6 September 2025
*/

#ifndef SIM_COMPUTE_REPLACEMENT_LTI_h
#define SIM_COMPUTE_REPLACEMENT_LTI_h

#include "sim/cmp/replacement.h"

#include <array>
#include <optional>
#include <utility>

namespace sim
{
namespace cmp
{
namespace repl
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
    Least Timely Instruction replacement. The idea is to evict the qubit
    with the instruction that "appears" farthest into the future.

    This is a bit different from Belady's min since we don't actually
    know if it will be the farthest into the future, but it's pretty close.
*/
struct LTI : public REPLACEMENT_POLICY_BASE
{
    using timeliness_type = int32_t;

    struct dominator_table_entry_type
    {
        CLIENT::inst_ptr winner;
        CLIENT::inst_ptr loser;
        uint64_t last_access;
    };

    // at some point we need to break ties between instructions with the same timeliness
    // so we do this on a first-come-first-serve basis
    // implemented as a fixed length array to avoid memory problems in long programs (evict via LRU)
    std::array<std::optional<dominator_table_entry_type>, 1024> domination_table;
    size_t dom_count{0};

    LTI(COMPUTE*);
    
    void update_on_use(QUBIT) override {}  // don't need to do anything
    std::optional<QUBIT> select_victim(QUBIT) override;

    timeliness_type compute_instruction_timeliness(QUBIT) const;

    bool check_for_domination(CLIENT::inst_ptr, CLIENT::inst_ptr);
    void create_domination_entry(CLIENT::inst_ptr, CLIENT::inst_ptr);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace repl
}   // namespace cmp
}   // namespace sim

#endif  // SIM_COMPUTE_REPLACEMENT_LTI_h