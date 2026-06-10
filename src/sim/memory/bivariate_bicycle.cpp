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

BB_MEMORY::BB_MEMORY(double freq_khz, size_t qubit_count, size_t n, size_t k, size_t d)
    :MEMORY_LEVEL(_name(n,k,d), freq_khz, qubit_count, n, k, d),
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
BB_MEMORY::next_ready_cycle_for_load(QUBIT* q) const
{
    size_t idx = 0;
    for (size_t i = 0; i < blocks_.size(); i++)
    {
        if (blocks_[i].count(q))
        {
            idx = i;
            break;
        }
    }

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
BB_MEMORY::log_fidelity(CLIENT* c, double scale, double d_freq_khz, double p) const
{
    const double cycles = convert_cycles_between_frequencies(c->s_cycle_complete, d_freq_khz, freq_khz) * scale;
    const double surgery_ops = s_surgery_operations * scale;
    const double aut_ops = s_shift_automorphisms * scale;

    const double ber_per_d_cycles = configuration::bivariate_bicycle_code_block_error_rate(storage_code_distance, p);
    const double surgery_error_per_op = ber_per_d_cycles*100,
                 aut_error_per_op = ber_per_d_cycles*10;

    // memory (idle) fidelity
    const double log_f_mem = num_blocks * mean(cycles, storage_code_distance) * std::log(1-ber_per_d_cycles);
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

MEMORY_ACCESS_RESULT
BB_MEMORY::load_impl(size_t idx, storage_type&, QUBIT* q)
{
    const size_t d{storage_code_distance};

    // adapter has to be ready to serve access:
    if (adapters_[idx] > current_cycle())
        return MEMORY_ACCESS_RESULT{};

    // identify latency of surgery operation:
    cycle_type latency = get_latency_of_surgery_operation(idx);
    MEMORY_ACCESS_RESULT out{.latency=latency, .freq_khz=freq_khz};

    // check if we can lock the routing space for the surgery operation:
    bool surgery_ok = routing_.test_local_resource(idx, current_cycle(), current_cycle() + latency);

    // routing space out of the memory is also locked for d cycles
    bool exit_ok = routing_.test_resources_between(
                                idx, routing::MCB_LEFT_ENTRY, current_cycle()+latency, current_cycle()+latency+d);

    if (surgery_ok && exit_ok)
    {
        out.success = true;
        routing_.lock_local_resource(idx, current_cycle(), current_cycle()+latency);
        routing_.lock_resources_between(
                            idx, routing::MCB_LEFT_ENTRY, current_cycle()+latency, current_cycle()+latency+d);
        adapters_[idx] = current_cycle() + latency;

        s_surgery_operations++;
        s_shift_automorphisms += (latency > storage_code_distance) ? 1 : 0;
    }

    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MEMORY_ACCESS_RESULT
BB_MEMORY::store_impl(size_t idx, storage_type&, QUBIT* q)
{
    if (adapters_[idx] > current_cycle())
        return MEMORY_ACCESS_RESULT{};

    cycle_type latency = get_latency_of_surgery_operation(idx);

    // unlike load, since store does not affect the execution of dependent instructions,
    // latency exposed to user is 0. But, it does lock down routing resources
    MEMORY_ACCESS_RESULT out{.freq_khz=freq_khz};

    // check if we can route to the block
    bool ok = routing_.test_resources_between(idx, routing::MCB_LEFT_ENTRY, current_cycle(), current_cycle()+latency);
    if (ok)
    {
        out.success =true;
        routing_.lock_resources_between(idx, routing::MCB_LEFT_ENTRY, current_cycle(), current_cycle()+latency);
        adapters_[idx] = current_cycle()+latency;
        s_surgery_operations++;
        s_shift_automorphisms += (latency > storage_code_distance) ? 1 : 0;
    }

    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MEMORY_ACCESS_RESULT
BB_MEMORY::coupled_load_store_impl(size_t idx, storage_type&, QUBIT* ld, QUBIT* st)
{
    const size_t d{storage_code_distance};

    if (adapters_[idx] > current_cycle())
        return MEMORY_ACCESS_RESULT{};

    // identify latency of load surgery:
    cycle_type ld_latency = get_latency_of_surgery_operation(idx);

    MEMORY_ACCESS_RESULT out{.latency=ld_latency, .freq_khz=freq_khz};

    bool ld_surgery_ok = routing_.test_local_resource(idx, current_cycle(), current_cycle()+ld_latency);
    bool exit_and_st_ok = routing_.test_resources_between(
                            idx, routing::MCB_LEFT_ENTRY, current_cycle()+ld_latency, current_cycle()+ld_latency+2*d);
    if (ld_surgery_ok && exit_and_st_ok)
    {
        out.success = true;
        routing_.lock_local_resource(idx, current_cycle(), current_cycle()+ld_latency);
        routing_.lock_resources_between(
                            idx, routing::MCB_LEFT_ENTRY, current_cycle()+ld_latency, current_cycle()+ld_latency+2*d);
        adapters_[idx] = current_cycle()+ld_latency+2*d;
        s_surgery_operations += 2;
        s_shift_automorphisms += (ld_latency > d) ? 1 : 0;
    }

    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

cycle_type
BB_MEMORY::get_latency_of_surgery_operation(size_t idx) const
{
    cycle_type a = adapters_[idx];
    assert(a <= current_cycle());
    cycle_type latency = storage_code_distance;
    if (current_cycle() - a < 2)
        latency += 2 - (current_cycle() - a);  // shift automorphism latency (max 2 cycles)
                                               // we assume that it can be hidden if the adapter
                                               // is ready much earlier than the current cycle
    return latency;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

constexpr size_t
_channel_width(size_t num_channels, size_t total_blocks)
{
    size_t blocks_per_channel = std::ceil(mean(total_blocks, num_channels));
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
