/*
 *  author: Suhas Vittal
 *  date:   18 February 2026
 * */

#include "sim/production.h"
#include "sim/configuration/resource_estimation.h"

#include <cassert>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using level_type = BulkProducer::level_type;
using event_type = BulkProducer::level_type::event_type;

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

level_type::level_type(ProducerQuantum q, size_t n)
    :traits{q},
    producer_count{n},
    unified_buffer_occupancy{0},
    unified_buffer_capacity{ n * q.buffer_size },
    next_available_cycle(n, 0),
    preq_resource_count(n, 0)
{}

void
level_type::add_event(size_t p_id, size_t n, cycle_type c)
{
    event_queue.push_back(event_type{ p_id, n, c });
    // producer can only become busy when the event is popped
    next_available_cycle[p_id] = std::numeric_limits<cycle_type>::max();
    preq_resource_count[p_id] = 0;
}

bool
level_type::pop_event(cycle_type c)
{
    if (event_queue.empty())
        return false;
    auto e = event_queue.front();
    if (e.ready_cycle <= c)
    {
        unified_buffer_occupancy += e.count;
        next_available_cycle[e.prod_id] = c + traits.restart_delay;
        event_queue.pop_front();
        return true;
    }
    else
    {
        return false;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

BulkProducer::BulkProducer(std::string_view name, double freq_khz)
    :Operable(name, freq_khz)
{}

long
BulkProducer::operate()
{
    long progress{};
    for (ssize_t i = levels_.size()-1; i >= 0; i--)
    {
        auto& lvl = levels_[i];
        progress += handle_resource_completions(lvl, i);
        progress += handle_resource_initiations(lvl, i);
    }
    return progress;
}

void
BulkProducer::add_level(ProducerQuantum q, size_t n)
{
    if (levels_.size() > 0 && levels_.back().traits.resource != q.resource)
        std::cerr << "BulkProducer::add_level(): tried to mix resources" << _die{};
    if (q.buffer_size < q.d_output_states)
    {
        std::cerr << "BulkProducer::add_level(): buffer_size (" << q.buffer_size << ") cannot hold"
                    << " a single production batch (d_output_states = " << q.d_output_states << ")" << _die{};
    }
    if (levels_.size() > 0 && levels_.back().traits.infidelity <= q.infidelity)
    {
        std::cerr << "BulkProducer::add_level(): levels must be added in order of increasing fidelity"
                    << _die{};
    }
    levels_.push_back(level_type(q, n));
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
BulkProducer::consume(size_t count)
{
    auto& top = levels_.back();
    if (top.unified_buffer_occupancy < count)
        return false;
    top.unified_buffer_occupancy -= count;
    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
BulkProducer::physical_qubit_count() const
{
    size_t total{0};
    for (const auto& lvl : levels_)
        total += lvl.traits.physical_qubit_count * lvl.producer_count;
    return total;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::optional<cycle_type>
BulkProducer::next_event_cycle() const
{
    std::optional<cycle_type> earliest{};
    for (const auto& lvl : levels_)
    {
        if (lvl.event_queue.empty())
            continue;
        cycle_type c = lvl.event_queue.front().ready_cycle;
        if (!earliest.has_value() || c < *earliest)
            earliest = c;
    }
    return earliest;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
BulkProducer::print_deadlock_info(std::ostream& out) const
{
    for (size_t i = 0; i < levels_.size(); i++)
    {
        const auto& lvl = levels_[i];
        out << name << ": L" << i << ": buffer = " << lvl.unified_buffer_occupancy
            << "/" << lvl.unified_buffer_capacity
            << ", producer_count = " << lvl.producer_count
            << ", pending_event_deltas = [";

        size_t shown{0};
        for (const auto& e : lvl.event_queue)
        {
            if (shown == 5)
            {
                out << " ...";
                break;
            }
            if (shown > 0)
                out << ",";
            out << " " << ( static_cast<int64_t>(e.ready_cycle) - static_cast<int64_t>(current_cycle()) );
            shown++;
        }
        out << " ], total = " << lvl.event_queue.size() << "\n";
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

long
BulkProducer::handle_resource_completions(level_type& lvl, ssize_t i)
{
    // assumption: cycles are ordered
    long progress{0};
    while (!lvl.event_queue.empty() && lvl.occupancy_remaining() >= lvl.event_queue.front().count)
    {
        if (lvl.pop_event(current_cycle()))
        {
            lvl.s_completions++;
            progress++;
        }
        else
        {
            break;
        }
    }
    return progress;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

long
BulkProducer::handle_resource_initiations(level_type& lvl, ssize_t i)
{
    if (lvl.unified_buffer_occupancy >= lvl.unified_buffer_capacity)
    {
        lvl.s_cycles_stalled_on_buffer++;
        return 1;
    }

    if (lvl.traits.is_distiller && i > 0 && levels_[i-1].unified_buffer_occupancy < lvl.traits.d_input_states)
        lvl.s_cycles_stalled_on_input++;

    long progress{0};
    for (size_t j = 0; j < lvl.producer_count; j++)
    {
        if (lvl.next_available_cycle[j] > current_cycle())
            continue;

        cycle_type ready_cycle = current_cycle() + lvl.traits.cycles_per_production;
        if (lvl.traits.is_distiller)
        {
            if (i > 0)
            {
                size_t states_required = lvl.traits.d_input_states - lvl.preq_resource_count[j];
                // Add input states to the current level (if it is not the lowest level)
                // Only proceed if the number of input states is equal to the amount required.
                size_t input_count = std::min(states_required, 
                                              levels_[i-1].unified_buffer_occupancy);
                lvl.preq_resource_count[j] += input_count;
                levels_[i-1].unified_buffer_occupancy -= input_count;
                if (lvl.preq_resource_count[j] < lvl.traits.d_input_states)
                    continue;
            }
            lvl.add_event(j, lvl.traits.d_output_states, ready_cycle);
        }
        else
        {
            lvl.add_event(j, 1, ready_cycle);
        }
        progress++;
    }
    return progress;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

cycle_type
retry_adjusted_latency(cycle_type c, size_t n, double e)
{
    const double p_succ = std::pow(1-e,n); 
    const size_t tr = static_cast<size_t>( std::ceil(1.0/p_succ) );
    return (c/2)*(tr-1) + c;
}

#include "production/t_quantum.inl"
#include "production/ed_quantum.inl"

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim
