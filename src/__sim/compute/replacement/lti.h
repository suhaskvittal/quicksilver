/*
    author: Suhas Vittal
    date:   6 September 2025
*/

#ifndef SIM_COMPUTE_REPLACEMENT_LTI_h
#define SIM_COMPUTE_REPLACEMENT_LTI_h

#include "sim/compute/replacement.h"

namespace sim
{
namespace compute
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
    using REPLACEMENT_POLICY_BASE::output_type;
    using timeliness_type = int32_t;

    LTI(COMPUTE*);
    
    void update_on_use(const CLIENT::qubit_info_type&) override {}  // don't need to do anything
    output_type select_victim(const CLIENT::qubit_info_type&) override;

    timeliness_type compute_instruction_timeliness(const CLIENT::qubit_info_type&) const;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace repl
}   // namespace compute
}   // namespace sim

#endif  // SIM_COMPUTE_REPLACEMENT_LTI_h