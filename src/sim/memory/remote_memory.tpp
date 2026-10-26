/*
 *  author: Suhas Vittal
 *  date:   7 September 2026
 * */

#include <algorithm>
#include <cmath>
#include <numeric>

#define TEMPL_PARAMS template <class IMPL>
#define TEMPL_CLASS  RemoteMemory<IMPL>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS
TEMPL_CLASS::RemoteMemory(double freq_khz,
                            size_t qubit_count,
                            size_t n,
                            size_t k,
                            size_t d,
                            BulkProducer* epr_producer)
    :IMPL(freq_khz, qubit_count, n, k, d),
    epr_producer_(epr_producer),
    epr_producer_adapter_ready_(epr_adapter_count(), 0)
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS cycle_type
TEMPL_CLASS::next_ready_cycle_for_load(Qubit* q) const
{
    cycle_type ready_cycle = IMPL::next_ready_cycle_for_load(q);

    // credit arrival cannot be predicted ahead of time (it depends on the producer's
    // event queue), so if the block is out of credits, simply refuse to report the
    // access as ready now.
    if (epr_producer_->top_level_buffer_occupancy() == 0)
        ready_cycle = std::max(ready_cycle, this->current_cycle() + 1);

    return ready_cycle;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS cycle_type
TEMPL_CLASS::next_ready_cycle_for_access(Qubit* q, Instruction::Type t) const
{
    cycle_type ready = IMPL::next_ready_cycle_for_load(q);
    if (!epr_producer_adapter_ready_.empty())
        ready = std::max(ready, *std::min_element(epr_producer_adapter_ready_.begin(),
                                                epr_producer_adapter_ready_.end()));
    const size_t required = t == Instruction::Type::COUPLED_LOAD_STORE ? 2 : 1;
    if (epr_producer_->top_level_buffer_occupancy() < required)
    {
        const auto event = epr_producer_->next_event_cycle();
        const auto retry = event ? convert_cycles_between_frequencies(*event, epr_producer_->freq_khz,
                                                                      this->freq_khz)
                                 : this->current_cycle()+1;
        ready = std::max(ready, std::max(this->current_cycle()+1, retry));
    }
    return ready;
}

TEMPL_PARAMS double
TEMPL_CLASS::log_fidelity(Client* c, double scale, double d_freq_khz, double p) const
{
    double log_f = IMPL::log_fidelity(c, scale, d_freq_khz, p);
    // each EPR pair spent on an access injects the ED protocol's output infidelity
    log_f += s_epr_consumed * scale * std::log(1.0 - epr_producer_->top_level_infidelity());
    return log_f;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS bool
TEMPL_CLASS::has_epr_credit(Qubit* q, Instruction::Type t) const
{
    const size_t required = (t == Instruction::Type::COUPLED_LOAD_STORE) ? 2 : 1;
    return epr_producer_->top_level_buffer_occupancy() >= required;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS MemoryAccessResult
TEMPL_CLASS::load_impl(size_t idx, storage_type& s, Qubit* q)
{
    const size_t d = this->storage_code_distance;
    // Block adapter availability takes priority over a credit stall.
    if (this->adapters_[idx] > this->current_cycle())
        return MemoryAccessResult{};

    // check if credits are available in storage
    if (!can_consume_epr(1))
    {
        s_cycles_stalled_on_epr++;
        return MemoryAccessResult{.code = MemoryAccessResult::Code::FAIL_NO_CREDIT};
    }
    auto out = IMPL::load_impl(idx, s, q);
    if (out.success())
        consume_epr(1, 2*d);
    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS MemoryAccessResult
TEMPL_CLASS::store_impl(size_t idx, storage_type& s, Qubit* q)
{
    const size_t d = this->storage_code_distance;
    // A teleportation store needs the block adapter immediately.
    if (this->adapters_[idx] > this->current_cycle())
        return MemoryAccessResult{};

    // check if credits are available in storage
    if (!can_consume_epr(1))
    {
        s_cycles_stalled_on_epr++;
        return MemoryAccessResult{.code = MemoryAccessResult::Code::FAIL_NO_CREDIT};
    }
    auto out = IMPL::store_impl(idx, s, q);
    if (out.success())
        consume_epr(1, 2*d);
    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS MemoryAccessResult
TEMPL_CLASS::coupled_load_store_impl(size_t idx, storage_type& s, Qubit* ld, Qubit* st)
{
    const size_t d = this->storage_code_distance;
    if (this->adapters_[idx] > this->current_cycle())
        return MemoryAccessResult{};

    if (!can_consume_epr(2))
    {
        s_cycles_stalled_on_epr++;
        return MemoryAccessResult{.code = MemoryAccessResult::Code::FAIL_NO_CREDIT};
    }

    auto out = IMPL::coupled_load_store_impl(idx, s, ld, st);
    if (out.success())
        consume_epr(2, 3*d);
    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS bool
TEMPL_CLASS::can_consume_epr(size_t n) const
{
    if (epr_producer_adapter_ready_.empty())
        return epr_producer_->top_level_buffer_occupancy() >= n;
    auto begin = epr_producer_adapter_ready_.begin(),
         end = epr_producer_adapter_ready_.end();
    auto it = std::find_if(begin, end,
                            [this] (cycle_type c) { return c <= this->current_cycle(); });
    if (it == end)
    {
        return false;
    }
    else
    {
        epr_adapter_idx_ = std::distance(begin, it);
        return epr_producer_->top_level_buffer_occupancy() >= n;
    }
}


TEMPL_PARAMS void
TEMPL_CLASS::consume_epr(size_t n, cycle_type latency)
{
    if (!epr_producer_adapter_ready_.empty())
        epr_producer_adapter_ready_[epr_adapter_idx_] = this->current_cycle() + latency;
    epr_producer_->consume(n);
    s_epr_consumed += n;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS size_t
TEMPL_CLASS::epr_adapter_count() const
{
    // Ideal distribution has no distillation-adapter contention; the block's
    // own adapter still limits accesses to that block.
    if (!epr_producer_->top_level_traits().is_distiller)
        return 0;
    const size_t k = this->storage_logical_qubit_count;
    const size_t nq = epr_producer_->top_level_traits().d_input_states * epr_producer_->top_level_count();
    return (nq+(k-1)) / k;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#undef TEMPL_PARAMS
#undef TEMPL_CLASS
