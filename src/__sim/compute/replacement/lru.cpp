/*
    author: Suhas Vittal
    date:   6 September 2025
*/

#include "sim/compute/replacement/lru.h"
#include "sim/compute.h"

namespace sim
{
namespace compute
{
namespace repl
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

LRU::LRU(COMPUTE* c) 
    :REPLACEMENT_POLICY_BASE(c)
{
    const auto& clients = c->clients();

    for (const auto& c : clients)
        last_use.emplace_back(c->qubits.size(), 0);
}

void
LRU::update_on_use(const CLIENT::qubit_info_type& q)
{
    last_use[q.memloc_info.client_id][q.memloc_info.qubit_id] = count;
    count++;
}

LRU::output_type
LRU::select_victim(const CLIENT::qubit_info_type& requested)
{
    const auto& clients = cmp->clients();

    output_type victim{nullptr};
    uint64_t victim_cycle{std::numeric_limits<uint64_t>::max()};
    size_t k{0};
    for (size_t i = 0; i < clients.size(); i++)
    {
        auto& c = clients.at(i);
        for (size_t j = 0; j < c->qubits.size(); j++)
        {
            auto& q = c->qubits.at(j);
            if (is_valid_victim(q, requested) && last_use[i][j] < victim_cycle)
            {
                victim_cycle = last_use[i][j];
                victim = &q;
            }
        }
    }

    return victim;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace repl
}   // namespace compute
}   // namespace sim