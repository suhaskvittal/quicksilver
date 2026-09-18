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
{
    // a remote region is not reached over the on-chip bus, so there is no routing
    // space to contend for, nor a bus transit phase in the access latency.
    if constexpr (std::is_same<IMPL, BBMemory>::value)
    {
        this->enable_routing_ = false;
        this->is_remote_ = true;
    }
}

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
    // unavailability (adapter not ready) takes priority over a credit stall: with
    // routing disabled for remote memory, the adapter check is IMPL's entire
    // availability condition, so we can read it directly without invoking IMPL.
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
    // matches BBMemory::store_impl's gate: locally the adapter only needs to be free by the
    // end of the transit phase; remotely there is no transit phase, so this reduces to
    // `current_cycle()` on its own.
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
    auto it = std::find_if(epr_producer_adapter_ready_.begin(), epr_producer_adapter_ready_.end(),
                            [this] (cycle_type c) { return c <= this->current_cycle(); });
    if (it == epr_producer_adapter_ready_.end())
        return false;
    else
        return epr_producer_->top_level_buffer_occupancy() >= n;
}


TEMPL_PARAMS void
TEMPL_CLASS::consume_epr(size_t n, cycle_type latency)
{
    auto it = std::find_if(epr_producer_adapter_ready_.begin(), epr_producer_adapter_ready_.end(),
                            [this] (cycle_type c) { return c <= this->current_cycle(); });
    if (it == epr_producer_adapter_ready_.end())
        std::cerr << "RemoteMemory::consume_epr(): no adapter available" << _die{};
    *it = this->current_cycle() + latency;
    epr_producer_->consume(n);
    s_epr_consumed += n;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS size_t
TEMPL_CLASS::epr_adapter_count() const
{
    const size_t k = this->storage_logical_qubit_count;
    const size_t nq = epr_producer_->top_level_traits().d_input_states * epr_producer_->top_level_count();
    return (nq+(k-1)) / k;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#undef TEMPL_PARAMS
#undef TEMPL_CLASS
