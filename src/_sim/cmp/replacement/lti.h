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

    LTI(COMPUTE*);
    
    void update_on_use(QUBIT) override {}  // don't need to do anything
    void update_on_fill(QUBIT) override {}  // don't need to do anything
    std::optional<QUBIT> select_victim(QUBIT, bool is_prefetch) override;

    timeliness_type compute_instruction_timeliness(QUBIT) const;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace repl
}   // namespace cmp
}   // namespace sim

#endif  // SIM_COMPUTE_REPLACEMENT_LTI_h