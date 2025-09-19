/*
    author: Suhas Vittal
    date:   6 September 2025
*/

#include "sim/cmp/replacement/lru.h"
#include "sim/compute.h"

namespace sim
{
namespace cmp
{
namespace repl
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

LRU::LRU(COMPUTE* c) 
    :REPLACEMENT_POLICY_BASE(c)
{
    last_use.reserve(128);
}

void
LRU::update_on_use(QUBIT q)
{
    last_use[q] = count;
    count++;
}

std::optional<QUBIT>
LRU::select_victim(QUBIT requested)
{
    const auto& clients = cmp->get_clients();

    std::optional<QUBIT> victim{std::nullopt};
    uint64_t victim_cycle{std::numeric_limits<uint64_t>::max()};
    for (const auto& c : clients)
    {
        for (qubit_type qid = 0; qid < static_cast<qubit_type>(c->num_qubits); qid++)
        {
            QUBIT q{c->id, qid};
            if (is_valid_victim(q, requested) && (!victim.has_value() || last_use[q] < victim_cycle))
            {
                victim_cycle = last_use[q];
                victim = q;
            }
        }
    }

    return victim;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace repl
}   // namespace cmp
}   // namespace sim