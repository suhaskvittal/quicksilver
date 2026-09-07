/*
 *  author: Suhas Vittal
 *  date:   18 February 2026
 * */

#include "sim/production.h"

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
    next_available_cycle(n, 0)
{}

bool
level_type::pop_event(cycle_type c)
{
    if (event_queue.front() < c)
    {
        event_queue.pop_front();
        lvl.unified_buffer_occupancy++;
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
    for (ssize_t i = levels_.size()-1; i >= 0; i++)
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
    levels_.push_bacK(level_type(q, n));
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

long
BulkProducer::handle_resource_completions(level_type& lvl, ssize_t i)
{
    // assumption: cycles are ordered
    long progress{0};
    while (lvl.occupancy_remaining() > 0 && lvl.event_queue.size() > 0)
    {
        if (lvl.pop_event(current_cycle()))
            progress++;
        else
            break;
    }
    return progress;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

long
BulkProducer::handle_resource_initiations(level_type& lvl, ssize_t i)
{
    long progress{0};
    for (size_t j = 0; j < producer_count; j++)
    {
        if (i > 0 && levels_[i-1].unified_buffer_occupancy < lvl.traits.d_input_states)
            break;

        if (lvl.next_available_cycle[j] > current_cycle())
            continue;

        cycle_type ready_cycle = current_cycle() + lvl.cycles_per_production;
        if (lvl.is_distiller)
        {
            if (i > 0)
                levels_[i-1].unified_buffer_occupancy -= lvl.traits.d_input_states;
            lvl.event_queue.push_back(event_type{ lvl.traits.d_output_states, ready_cycle });
        }
        else
        {
            lvl.event_queue.push_back(event_type{ 1, ready_cycle });
        }
        lvl.next_available_cycle[j] = ready_cycle+1;
        progress++;
    }
    return progress;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#include "production/t_quantum.inl"

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim
