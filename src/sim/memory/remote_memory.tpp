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
    :IMPL(freq_khz, qubit_count, n, k - EPR_CREDITS_PER_BLOCK, d),
    credits_in_block_(this->num_blocks, 0),
    epr_producer_(epr_producer)
{
    // a remote region is not reached over the on-chip bus, so there is no routing
    // space to contend for.
    if constexpr (std::is_same<IMPL, BBMemory>::value)
        this->enable_routing_ = false;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS long
TEMPL_CLASS::operate()
{
    long progress{0};

    // check if EPR producer has generated any EPR pairs. place them into the
    // block with the fewest pairs.
    for (size_t i = 0; i < this->num_blocks; i++)
    {
        if (epr_producer_->top_level_buffer_occupancy() == 0)
            break;
        if (credits_in_block_[i] >= EPR_CREDITS_PER_BLOCK)
            continue;

        if constexpr (std::is_same<IMPL, BBMemory>::value)
        {
            if (this->adapters_[i] > this->current_cycle())
                continue;
            this->adapters_[i] = this->current_cycle() + this->storage_code_distance;
        }

        credits_in_block_[i]++;
        epr_producer_->consume(1);
        progress++;
    }

    return progress + IMPL::operate();
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
    if (credits_in_block_[this->block_index_of(q)] < 1)
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
TEMPL_CLASS::has_epr_credit(Qubit* q) const
{
    return credits_in_block_[this->block_index_of(q)] >= 2;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS MemoryAccessResult
TEMPL_CLASS::load_impl(size_t idx, storage_type& s, Qubit* q)
{
    // unavailability (adapter not ready) takes priority over a credit stall: with
    // routing disabled for remote memory, the adapter check is IMPL's entire
    // availability condition, so we can read it directly without invoking IMPL.
    if (this->adapters_[idx] > this->current_cycle())
        return MemoryAccessResult{};

    // check if credits are available in storage
    if (credits_in_block_[idx] < 1)
    {
        s_cycles_stalled_on_epr++;
        return MemoryAccessResult{.code = MemoryAccessResult::Code::FAIL_NO_CREDIT};
    }
    auto out = IMPL::load_impl(idx, s, q);
    if (out.success())
    {
        credits_in_block_[idx]--;
        s_epr_consumed++;
    }
    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS MemoryAccessResult
TEMPL_CLASS::store_impl(size_t idx, storage_type& s, Qubit* q)
{
    if (this->adapters_[idx] > this->current_cycle())
        return MemoryAccessResult{};

    // check if credits are available in storage
    if (credits_in_block_[idx] < 1)
    {
        s_cycles_stalled_on_epr++;
        return MemoryAccessResult{.code = MemoryAccessResult::Code::FAIL_NO_CREDIT};
    }
    auto out = IMPL::store_impl(idx, s, q);
    if (out.success())
    {
        credits_in_block_[idx]--;
        s_epr_consumed++;
    }
    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS MemoryAccessResult
TEMPL_CLASS::coupled_load_store_impl(size_t idx, storage_type& s, Qubit* ld, Qubit* st)
{
    if (this->adapters_[idx] > this->current_cycle())
        return MemoryAccessResult{};

    // check if credits are available in storage
    if (credits_in_block_[idx] < 2)
    {
        s_cycles_stalled_on_epr++;
        return MemoryAccessResult{.code = MemoryAccessResult::Code::FAIL_NO_CREDIT};
    }
    auto out = IMPL::coupled_load_store_impl(idx, s, ld, st);
    if (out.success())
    {
        credits_in_block_[idx] -= 2;
        s_epr_consumed += 2;
    }
    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#undef TEMPL_PARAMS
#undef TEMPL_CLASS
