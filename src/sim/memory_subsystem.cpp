/*
 *  author: Suhas Vittal
 *  date:   12 January 2026
 * */

#include "sim/memory_subsystem.h"
#include "sim/routing_model/multi_channel_bus.h"

#include <algorithm>
#include <cassert>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using routing_ptr = std::unique_ptr<MEMORY_SUBSYSTEM::routing_base_type>;

template <class ITER>
ITER _lookup_qubit(ITER begin, ITER end, QUBIT*);

template <class ITER>
ITER _find_empty_storage(ITER begin, ITER end, const routing_ptr&, cycle_type current_cycle);


} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MEMORY_SUBSYSTEM::MEMORY_SUBSYSTEM(std::vector<STORAGE*>&& storages)
    :storages_(std::move(storages))
{
    using MBC = routing::MULTI_CHANNEL_BUS<STORAGE>;
    routing_ = std::make_unique<MBC>(storages_, 2);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MEMORY_SUBSYSTEM::access_result_type
MEMORY_SUBSYSTEM::do_load(QUBIT* q, cycle_type c_current_cycle, double c_freq_khz)
{
    auto s_it = _lookup_qubit(storages_.begin(), storages_.end(), q);
    if (s_it == storages_.end())
    {
        std::cerr << "MEMORY_SUBSYSTEM::do_memory_access: qubit " << *q << " not found";
        for (auto* s : storages_)
        {
            std::cerr << "\n\t" << s->name << " :";
            for (auto* x : s->contents())
                std::cerr << " " << *x;
        }
        std::cerr << _die{};
    }

    if (routing_->can_route_to(*s_it, c_current_cycle))
        return handle_access_outcome((*s_it)->do_load(q), *s_it, c_current_cycle, c_freq_khz);
    return access_result_type{};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MEMORY_SUBSYSTEM::access_result_type
MEMORY_SUBSYSTEM::do_store(QUBIT* q, cycle_type c_current_cycle, double c_freq_khz)
{
    auto s_it = _find_empty_storage(storages_.begin(), storages_.end(), routing_, c_current_cycle);
    if (s_it != storages_.end())
        return handle_access_outcome((*s_it)->do_store(q), *s_it, c_current_cycle, c_freq_khz);
    return access_result_type{};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MEMORY_SUBSYSTEM::access_result_type
MEMORY_SUBSYSTEM::do_coupled_load_store(QUBIT* ld, QUBIT* st, cycle_type c_current_cycle, double c_freq_khz)
{
    auto s_it = _lookup_qubit(storages_.begin(), storages_.end(), ld);
    if (s_it == storages_.end())
    {
        std::cerr << "MEMORY_SUBSYSTEM::do_memory_access: qubit " << *ld << " not found";
        for (auto* s : storages_)
        {
            std::cerr << "\n\t" << s->name << " :";
            for (auto* x : s->contents())
                std::cerr << " " << *x;
        }
        std::cerr << _die{};
    }

    // coupled  access only succeeds if both load and store can occur
    if (!routing_->can_route_to(*s_it, c_current_cycle))
        return access_result_type{};
    return handle_access_outcome((*s_it)->do_coupled_load_store(ld, st), *s_it, c_current_cycle, c_freq_khz);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

QUBIT*
MEMORY_SUBSYSTEM::retrieve_qubit(client_id_type c_id, qubit_type q_id) const
{
    for (const auto* s : storages_)
    {
        const auto& contents = s->contents();
        auto q_it = std::find_if(contents.begin(), contents.end(),
                            [c_id, q_id] (const auto* q) { return q->client_id == c_id && q->qubit_id == q_id; });
        if (q_it != contents.end())
            return *q_it;
    }
    return nullptr;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

cycle_type
MEMORY_SUBSYSTEM::get_next_ready_cycle_for_load(QUBIT* q, double c_freq_khz) const
{
    auto s_it = _lookup_qubit(storages_.begin(), storages_.end(), q);
    assert(s_it != storages_.end());

    // `routing_free_cycle` is already a compute cycle, no need to convert
    cycle_type routing_free_cycle = routing_->ready_cycle(*s_it);

    // `storage_free_cycle` needs to be converted
    cycle_type storage_free_cycle = (*s_it)->next_free_adapter_cycle();
    storage_free_cycle = convert_cycles_between_frequencies(storage_free_cycle, (*s_it)->freq_khz, c_freq_khz);

    cycle_type out = std::max(routing_free_cycle, storage_free_cycle);
    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

const std::vector<STORAGE*>&
MEMORY_SUBSYSTEM::storages() const
{
    return storages_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MEMORY_SUBSYSTEM::access_result_type
MEMORY_SUBSYSTEM::handle_access_outcome(access_result_type result, 
                                        STORAGE* s, 
                                        cycle_type c_current_cycle, 
                                        double c_freq_khz)
{
    if (result.success)
    {
        result.latency = convert_cycles_between_frequencies(result.latency, result.storage_freq_khz, c_freq_khz);
        result.critical_latency = convert_cycles_between_frequencies(result.critical_latency, result.storage_freq_khz, c_freq_khz);

        cycle_type routing_cycles = convert_cycles_between_frequencies(2, result.storage_freq_khz, c_freq_khz);
        routing_->lock_route_to(s, c_current_cycle+routing_cycles);
    }
    return result;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

template <class ITER> ITER
_lookup_qubit(ITER begin, ITER end, QUBIT* q)
{
    return std::find_if(begin, end, [q] (STORAGE* s) { return s->contains(q); });
}

template <class ITER> ITER
_find_empty_storage(ITER begin, ITER end, const routing_ptr& r, cycle_type current_cycle)
{
    return std::find_if(begin, end, 
            [&r, current_cycle] (STORAGE* s)
            { 
                return s->contents().size() < s->logical_qubit_count && r->can_route_to(s, current_cycle);
            });
}


}  // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim
