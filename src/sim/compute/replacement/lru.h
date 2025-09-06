/*
    author: Suhas Vittal
    date:   6 September 2025
*/

#ifndef SIM_COMPUTE_REPLACEMENT_LRU_h
#define SIM_COMPUTE_REPLACEMENT_LRU_h

#include "sim/compute/replacement.h"

#include <cstdint>
#include <vector>

namespace sim
{
namespace compute
{
namespace repl
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct LRU : public REPLACEMENT_POLICY_BASE
{
    using REPLACEMENT_POLICY_BASE::output_type;

    std::vector<std::vector<uint64_t>> last_use;
    uint64_t count{0};

    LRU(COMPUTE*);

    void update_on_use(const CLIENT::qubit_info_type&) override;
    output_type select_victim(const CLIENT::qubit_info_type&) override;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace repl
}   // namespace compute
}   // namespace sim

#endif  // SIM_COMPUTE_REPLACEMENT_LRU_h