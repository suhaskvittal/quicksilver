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

constexpr size_t NUM_CHANNELS{4};

/*
 * Computes the width of each channel given number of channels
 * and total number of blocks in the entire system
 * */
constexpr size_t _channel_width(size_t num_channels, size_t total_blocks);

std::string _name(size_t, size_t, size_t);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

BBMemory::BBMemory(double freq_khz, size_t qubit_count, size_t n, size_t k, size_t d)
    :MemoryLevel(_name(n,k,d), freq_khz, qubit_count, n, k, d),
    routing_(NUM_CHANNELS, _channel_width(NUM_CHANNELS, num_blocks)),
    adapters_(num_blocks, 0)
{
    // define routing space:
    for (size_t i = 0; i < num_blocks; i++)
    {
        size_t ii{i};
        const int ch = ii % NUM_CHANNELS;
        ii /= NUM_CHANNELS;
        const int ro = ii & 1;
        ii >>= 1;
        const int co = ii;
        assert(co < routing_.channel_width);

        routing_.set_location(i, ch, ro, co);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

cycle_type
BBMemory::next_ready_cycle_for_load(Qubit* q) const
{
    size_t idx = block_index_of(q);

    cycle_type ready_cycle = adapters_[idx];
    routing_.for_each_resource_between(idx, routing::MCB_LEFT_ENTRY,
                        [this, &ready_cycle, d=storage_code_distance] (const auto& r)
                        {
                            ready_cycle = std::max(ready_cycle, r.next_ready_cycle(current_cycle(), d));
                        });
    return ready_cycle;
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
    const cycle_type d{storage_code_distance}, 
                     transit{transit_latency()};

    // surgery starts immediately, so the adapter has to be ready now:
    if (adapters_[idx] > current_cycle())
        return MemoryAccessResult{};

    // d rounds of surgery, plus the shift automorphism at the head of them
    cycle_type surgery = latency_of_surgery_operation(current_cycle(), idx);
    if (!enable_routing_)  // using teleportation -- need extra d cycles (two PPMs)
        surgery += d;

    // the loaded qubit is exposed as soon as surgery completes; the transit out of the block
    // that follows does not gate dependent instructions
    MemoryAccessResult out{.latency=surgery, .freq_khz=freq_khz};

    // check if we can lock the routing space for the surgery operation:
    bool surgery_ok{true};
    if (enable_routing_)
        surgery_ok = routing_.test_local_resource(idx, current_cycle(), current_cycle() + surgery);

    // routing space out of the memory is also locked for the transit phase
    bool exit_ok{true};
    if (enable_routing_)
    {
        exit_ok = routing_.test_resources_between(
                            idx, routing::MCB_LEFT_ENTRY, current_cycle()+surgery, current_cycle()+surgery+transit);
    }

    if (surgery_ok && exit_ok)
    {
        out.code = MemoryAccessResult::Code::SUCCESS;
        adapters_[idx] = current_cycle() + surgery + transit;

        if (enable_routing_)
        {
            routing_.lock_local_resource(idx, current_cycle(), current_cycle()+surgery);
            routing_.lock_resources_between(
                            idx, routing::MCB_LEFT_ENTRY, current_cycle()+surgery, current_cycle()+surgery+transit);
        }

        s_surgery_operations++;
        s_shift_automorphisms += (surgery > d) ? 1 : 0;
    }

    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MemoryAccessResult
BBMemory::store_impl(size_t idx, storage_type&, Qubit* q)
{
    const cycle_type d{storage_code_distance}, transit{transit_latency()};

    // the first `transit` cycles are spent routing to the block, so locally the adapter only
    // has to be free by then; remotely there is no routing phase, so it has to be free now.
    if (adapters_[idx] > current_cycle()+transit)
        return MemoryAccessResult{};

    // shift automorphism arises from the second d rounds (the surgery, once routed in)
    cycle_type surgery = latency_of_surgery_operation(current_cycle()+transit, idx);
    if (!enable_routing_)
        surgery = 0;  // no overhead with teleportation

    // unlike load, since store does not affect the execution of dependent instructions,
    // latency exposed to user is 0. But, it does lock down routing resources
    MemoryAccessResult out{.freq_khz=freq_khz};

    // check if we can route to the block
    bool ok{true};
    if (enable_routing_)
        ok = routing_.test_resources_between(idx, routing::MCB_LEFT_ENTRY, current_cycle(), current_cycle()+transit);
    if (ok)
    {
        out.code = MemoryAccessResult::Code::SUCCESS;
        if (enable_routing_)
            routing_.lock_resources_between(idx, routing::MCB_LEFT_ENTRY, current_cycle(), current_cycle()+transit);
        adapters_[idx] = current_cycle()+transit+surgery;
        s_surgery_operations++;
        s_shift_automorphisms += (surgery > d) ? 1 : 0;
    }

    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MemoryAccessResult
BBMemory::coupled_load_store_impl(size_t idx, storage_type&, Qubit* ld, Qubit* st)
{
    const cycle_type d{storage_code_distance}, transit{transit_latency()};

    if (adapters_[idx] > current_cycle())
        return MemoryAccessResult{};

    // by convention the load goes first, so it takes the one shift automorphism; the store
    // surgery that follows (once the store qubit has transited in) is a plain d rounds
    cycle_type surgery = latency_of_surgery_operation(current_cycle(), idx);
    if (!enable_routing_)  // extra PPM with teleportation (no overhead from st)
        surgery += d;

    MemoryAccessResult out{.latency=surgery, .freq_khz=freq_khz};

    bool ld_surgery_ok{true};
    if (enable_routing_)
        ld_surgery_ok = routing_.test_local_resource(idx, current_cycle(), current_cycle()+surgery);

    // load exit and store entry are contiguous on the bus
    bool exit_and_st_ok{true};
    if (enable_routing_)
    {
        exit_and_st_ok = routing_.test_resources_between(
                        idx, routing::MCB_LEFT_ENTRY, current_cycle()+surgery, current_cycle()+surgery+2*transit);
    }

    if (ld_surgery_ok && exit_and_st_ok)
    {
        out.code = MemoryAccessResult::Code::SUCCESS;

        if (enable_routing_)
        {
            routing_.lock_local_resource(idx, current_cycle(), current_cycle()+surgery);
            routing_.lock_resources_between(
                        idx, routing::MCB_LEFT_ENTRY, current_cycle()+surgery, current_cycle()+surgery+2*transit);
        }

        if (enable_routing_)
            adapters_[idx] = current_cycle()+surgery+2*transit+d; // load surgery, load exit, store entry, then the store's own d-round surgery
        else
            adapters_[idx] = current_cycle()+surgery;

        s_surgery_operations += 2;
        s_shift_automorphisms += (surgery > d) ? 1 : 0;
    }

    return out;
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

constexpr size_t
_channel_width(size_t num_channels, size_t total_blocks)
{
    size_t blocks_per_channel = std::ceil(fpdiv(total_blocks, num_channels));
    size_t w = blocks_per_channel >> 1;
    if (blocks_per_channel & 1)
        w++;
    return w;
}

std::string
_name(size_t n, size_t k, size_t d)
{
    return "BB_" + std::to_string(n) + "_" + std::to_string(k) + "_" + std::to_string(d);
}

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim
