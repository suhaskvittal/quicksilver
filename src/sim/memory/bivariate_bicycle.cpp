/*
 *  author: Suhas Vittal
 *  date:   11 March 2026
 * */

#include "sim/client.h"
#include "sim/configuration/resource_estimation.h"
#include "sim/memory/bivariate_bicycle.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

std::string _name(size_t, size_t, size_t);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

BBMemory::BBMemory(double freq_khz, size_t qubit_count, size_t n, size_t k, size_t d)
    :MemoryLevel(_name(n,k,d), freq_khz, qubit_count, n, k, d),
    adapters_(num_blocks, 0)
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

cycle_type
BBMemory::next_ready_cycle_for_load(Qubit* q) const
{
    return adapters_[block_index_of(q)];
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

double
BBMemory::log_fidelity(Client* c, double scale, double d_freq_khz, double p) const
{
    const double cycles = convert_cycles_between_frequencies(c->s_cycle_complete, d_freq_khz, freq_khz) * scale;
    const double surgery_ops = s_surgery_operations * scale;
    const double aut_ops = s_shift_automorphisms * scale;

    const double ber_per_d_cycles = configuration::bivariate_bicycle_code_block_error_rate(storage_code_distance, p);
    const double surgery_error_per_op = ber_per_d_cycles*100,
                 aut_error_per_op = ber_per_d_cycles*10;

    // memory (idle) fidelity
    const double log_f_mem = num_blocks * fpdiv(cycles, storage_code_distance) * std::log(1-ber_per_d_cycles);
    // surgery fidelity
    const double log_f_surgery = surgery_ops * std::log(1-surgery_error_per_op);
    // automorphism fidelity
    const double log_f_aut = aut_ops * std::log(1-aut_error_per_op);
    // total fidelity:
    const double log_f = log_f_mem + log_f_surgery + log_f_aut;
    return log_f;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MemoryAccessResult
BBMemory::load_impl(size_t idx, storage_type&, Qubit* q)
{
    if (adapters_[idx] > current_cycle())
        return MemoryAccessResult{};

    // Teleportation needs two d-round PPMs, plus any exposed automorphism.
    const cycle_type surgery = latency_of_surgery_operation(current_cycle(), idx)
                                 + storage_code_distance;
    adapters_[idx] = current_cycle() + surgery;
    s_surgery_operations += 2;
    s_shift_automorphisms++;
    return MemoryAccessResult{.code=MemoryAccessResult::Code::SUCCESS,
                              .latency=surgery, .freq_khz=freq_khz};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MemoryAccessResult
BBMemory::store_impl(size_t idx, storage_type&, Qubit* q)
{
    if (adapters_[idx] > current_cycle())
        return MemoryAccessResult{};

    const cycle_type surgery = latency_of_surgery_operation(current_cycle(), idx);
    adapters_[idx] = current_cycle() + surgery;
    s_surgery_operations++;
    s_shift_automorphisms++;
    // Stores do not delay dependent instructions, but occupy the block adapter.
    return MemoryAccessResult{.code=MemoryAccessResult::Code::SUCCESS, .freq_khz=freq_khz};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MemoryAccessResult
BBMemory::coupled_load_store_impl(size_t idx, storage_type&, Qubit* ld, Qubit* st)
{
    if (adapters_[idx] > current_cycle())
        return MemoryAccessResult{};

    const cycle_type ld_surgery = latency_of_surgery_operation(current_cycle(), idx)
                                    + storage_code_distance;
    // Load first, then the store's d-round surgery; no memory bus transit.
    adapters_[idx] = current_cycle() + ld_surgery + storage_code_distance;
    s_surgery_operations += 3;
    s_shift_automorphisms++;
    return MemoryAccessResult{.code=MemoryAccessResult::Code::SUCCESS,
                              .latency=ld_surgery, .freq_khz=freq_khz};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

cycle_type
BBMemory::latency_of_surgery_operation(cycle_type ref, size_t idx) const
{
    cycle_type a = adapters_[idx];
    assert(a <= ref);
    cycle_type latency = storage_code_distance;
    if (ref - a < SHIFT_AUT_LATENCY)
        latency += SHIFT_AUT_LATENCY - (ref - a);  // shift automorphism latency (max SHIFT_AUT_LATENCY cycles)
                                                   // we assume that it can be hidden if the adapter
                                                   // is ready much earlier than `ref`
    return latency;
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

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim
