/*
    author: Suhas Vittal
    date:   6 September 2025
*/

#ifndef SIM_COMPUTE_REPLACEMENT_LRU_h
#define SIM_COMPUTE_REPLACEMENT_LRU_h

#include "sim/cmp/replacement.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace sim
{
namespace cmp
{
namespace repl
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct LRU : public REPLACEMENT_POLICY_BASE
{
    std::unordered_map<QUBIT, uint64_t> last_use;
    uint64_t count{0};

    LRU(COMPUTE*);

    void update_on_use(QUBIT) override;
    void update_on_fill(QUBIT) override;
    std::optional<QUBIT> select_victim(QUBIT, bool is_prefetch) override;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace repl
}   // namespace cmp
}   // namespace sim

#endif  // SIM_COMPUTE_REPLACEMENT_LRU_h