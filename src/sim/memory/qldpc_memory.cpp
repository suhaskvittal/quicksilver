/*
 *  author: Suhas Vittal
 *  date:   11 March 2026
 * */

#include "sim/client.h"
#include "sim/configuration/resource_estimation.h"
#include "sim/memory/qldpc_memory.h"

#include <cmath>
#include <stdexcept>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

std::string _name(size_t, size_t, size_t);
size_t _epr_block_count(BulkProducer*, size_t k);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

QLDPCMemory::block_data_type::block_data_type(size_t n)
    :extractors(n, 0)
{}

auto
QLDPCMemory::block_data_type::ready_extractor(cycle_type c) -> std::vector<cycle_type>::iterator
{
    return std::find_if(extractors.begin(), extractors.end(), [c] (auto e) { return e <= c; });
}

auto
QLDPCMemory::block_data_type::ready_extractor(cycle_type c) const -> std::vector<cycle_type>::const_iterator
{
    return std::find_if(extractors.begin(), extractors.end(), [c] (auto e) { return e <= c; });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

QLDPCMemory::QLDPCMemory(double freq_khz, size_t qubit_count, size_t n, size_t k, size_t d)
    :QLDPCMemory(freq_khz, qubit_count, n, k, d, config_type{})
{}

QLDPCMemory::QLDPCMemory(double freq_khz, size_t qubit_count, size_t n, size_t k, size_t d,
                         BulkProducer* epr_producer)
    :QLDPCMemory(freq_khz, qubit_count, n, k, d, config_type{}, epr_producer)
{}

QLDPCMemory::QLDPCMemory(double freq_khz,
                    size_t qubit_count,
                    size_t n,
                    size_t k,
                    size_t d,
                    config_type _conf,
                    BulkProducer* epr_producer)
    :MemoryLevel(_name(n,k,d), freq_khz, qubit_count, n, k, d),
    conf(_conf),
    prog_block_data_(num_blocks, block_data_type{conf.extractors_per_block}),
    epr_block_data_{conf.extractors_per_block * _epr_block_count(epr_producer, k)},
    epr_producer_(epr_producer)
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

cycle_type
QLDPCMemory::next_ready_cycle_for_load(Qubit* q) const
{
    return next_ready_cycle_for_access(q, Instruction::Type::LOAD);
}

cycle_type
QLDPCMemory::next_ready_cycle_for_access(Qubit* q, Instruction::Type access) const
{
    const size_t idx = block_index_of(q);
    cycle_type ready = prog_block_data_[idx].next_ready_cycle();
    if (epr_producer_)
    {
        if (!epr_block_data_.extractors.empty())
            ready = std::max(ready, epr_block_data_.next_ready_cycle());
        if (!has_epr_credit(q, access))
        {
            const auto event = epr_producer_->next_event_cycle();
            const auto retry = event ? convert_cycles_between_frequencies(*event, epr_producer_->freq_khz, freq_khz)
                                     : current_cycle() + 1;
            ready = std::max(ready, std::max(current_cycle() + 1, retry));
        }
    }
    return ready;
}

bool
QLDPCMemory::has_epr_credit(Qubit*, Instruction::Type access) const
{
    if (!epr_producer_)
        return true;
    const size_t required = access == Instruction::Type::COUPLED_LOAD_STORE ? 2 : 1;
    return epr_producer_->top_level_buffer_occupancy() >= required;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

double
QLDPCMemory::log_fidelity(Client* c, double scale, double d_freq_khz, double p) const
{
    const double cycles = convert_cycles_between_frequencies(c->s_cycle_complete, d_freq_khz, freq_khz) * scale;
    const double surgery_ops = s_surgery_operations * scale;
    const double aut_ops = s_shift_automorphisms * scale;

    const double ber_per_d_cycles = configuration::bivariate_bicycle_code_block_error_rate(storage_code_distance, p);
    const double surgery_error_per_op = ber_per_d_cycles,  // this is a guess -- see concatenation of gross code with algebraic codes
                 aut_error_per_op = ber_per_d_cycles;

    // memory (idle) fidelity
    const double log_f_mem = num_blocks * fpdiv(cycles, storage_code_distance) * std::log(1-ber_per_d_cycles);
    // surgery fidelity
    const double log_f_surgery = surgery_ops * std::log(1-surgery_error_per_op);
    // automorphism fidelity
    const double log_f_aut = aut_ops * std::log(1-aut_error_per_op);
    // total fidelity:
    double log_f = log_f_mem + log_f_surgery + log_f_aut;
    if (epr_producer_)
        log_f += s_epr_consumed * scale * std::log(1.0 - epr_producer_->top_level_infidelity());
    return log_f;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MemoryAccessResult
QLDPCMemory::load_impl(size_t idx, storage_type&, Qubit* q)
{
    auto t = do_inter_block_surgery(idx, current_cycle(), 2);
    if (!t.has_value())
        return MemoryAccessResult();
    s_surgery_operations += 2;
    s_shift_automorphisms++;
    return MemoryAccessResult{.code=MemoryAccessResult::Code::SUCCESS, .latency=*t, .freq_khz=freq_khz};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MemoryAccessResult
QLDPCMemory::store_impl(size_t idx, storage_type&, Qubit* q)
{
    auto t = do_inter_block_surgery(idx, current_cycle(), 1);
    if (!t.has_value())
        return MemoryAccessResult();
    s_surgery_operations++;
    s_shift_automorphisms++;
    return MemoryAccessResult{.code=MemoryAccessResult::Code::SUCCESS, .latency=*t, .freq_khz=freq_khz};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MemoryAccessResult
QLDPCMemory::coupled_load_store_impl(size_t idx, storage_type&, Qubit* ld, Qubit* st)
{
    if (epr_producer_ && epr_producer_->top_level_buffer_occupancy() < 2)
        return MemoryAccessResult{};
    auto t = do_inter_block_surgery(idx, current_cycle(), 2);
    if (!t.has_value())
        return MemoryAccessResult();
    if (!do_inter_block_surgery(idx, current_cycle()+*t, 1, false))
        throw std::logic_error("QLDPCMemory: second coupled surgery could not be reserved");
    s_surgery_operations += 3;
    s_shift_automorphisms++;
    return MemoryAccessResult{.code=MemoryAccessResult::Code::SUCCESS,
                              .latency=*t, .freq_khz=freq_khz};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

cycle_type
QLDPCMemory::adjust_surgery_latency(cycle_type s, cycle_type r) const
{
    cycle_type t = surgery_latency();
    if (r <= s && (s-r) < conf.shift_aut_latency)
        t += conf.shift_aut_latency - (s-r);
    return t;
}

std::optional<cycle_type>
QLDPCMemory::do_inter_block_surgery(size_t idx, cycle_type start_cycle, size_t num_ops, bool shift_aut_needed)
{
    if (!prog_block_data_[idx].is_available(start_cycle))
        return std::nullopt;
    const bool needs_epr_extractor = epr_producer_ && !epr_block_data_.extractors.empty();
    if (epr_producer_)
    {
        if ((needs_epr_extractor && !epr_block_data_.is_available(start_cycle))
                || epr_producer_->top_level_buffer_occupancy() < 1)
        {
            return std::nullopt;
        }
    }
    auto e1_it = prog_block_data_[idx].ready_extractor(start_cycle),
         e2_it = epr_block_data_.ready_extractor(start_cycle);

    const cycle_type ext_ready_cycle = needs_epr_extractor ? std::max(*e1_it, *e2_it) : *e1_it;

    cycle_type t{0};
    // handle first op specially:
    t += shift_aut_needed ? adjust_surgery_latency(start_cycle, ext_ready_cycle) : surgery_latency();
    // remaining ops all take `surgery_latency()`
    t += (num_ops-1) * surgery_latency();

    *e1_it = start_cycle + t;
    if (epr_producer_)
    {
        if (needs_epr_extractor)
            *e2_it = start_cycle + t;
        epr_producer_->consume(1);
        ++s_epr_consumed;
    }
    return std::make_optional(t);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

std::string
_name(size_t n, size_t k, size_t d)
{
    return "BB_" + std::to_string(n) + "_" + std::to_string(k) + "_" + std::to_string(d);
}

size_t
_epr_block_count(BulkProducer* p, size_t k)
{
    if (p == nullptr)
        return 0;
    const size_t nq = p->top_level_traits().d_input_states * p->top_level_count();
    return (nq+(k-1))/k;
}

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim
