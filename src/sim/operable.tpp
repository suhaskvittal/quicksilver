/*
    author: Suhas Vittal
    date:   15 September 2025
*/

#include "sim/clock.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#define TEMPL_PARAMS    template <class EVENT_ID_TYPE, class EVENT_INFO_TYPE>
#define TEMPL_CLASS     OPERABLE<EVENT_ID_TYPE, EVENT_INFO_TYPE>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS 
TEMPL_CLASS::OPERABLE(double freq_khz) 
    :OP_freq_khz(freq_khz)
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS typename TEMPL_CLASS::event_type
TEMPL_CLASS::next_event() const
{
    return OP_event_queue.top();
}

TEMPL_PARAMS void
TEMPL_CLASS::pop_event()
{
    OP_event_queue.pop();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS void
TEMPL_CLASS::OP_process_event(event_type event)
{
    // update state -- convert twice because `cycle` may round up
    if (event.time_ns < GL_CURRENT_TIME_NS)
    {
        throw std::runtime_error("OP_process_event: event time is in the past, current time ns = " 
                                    + std::to_string(GL_CURRENT_TIME_NS) + ", event time ns = " + std::to_string(event.time_ns));
    }

    GL_CURRENT_TIME_NS = event.time_ns;

    // call subclass's implementation of the event:
    OP_handle_event(event);
}

TEMPL_PARAMS void
TEMPL_CLASS::OP_add_event(EVENT_ID_TYPE id, uint64_t time_ns_from_now, EVENT_INFO_TYPE info)
{
    // do a double conversion to align the time with thie operable's clock
    uint64_t final_time_ns = GL_CURRENT_TIME_NS + time_ns_from_now;
    uint64_t final_cycle = convert_ns_to_cycles(final_time_ns, OP_freq_khz);
    final_time_ns = convert_cycles_to_ns(final_cycle, OP_freq_khz);
    time_ns_from_now = final_time_ns - GL_CURRENT_TIME_NS;

    event_type event{id, GL_CURRENT_TIME_NS + time_ns_from_now, info};
    OP_event_queue.push(event);
}

TEMPL_PARAMS void
TEMPL_CLASS::OP_add_event_using_cycles(EVENT_ID_TYPE id, uint64_t cycles_from_now, EVENT_INFO_TYPE info)
{
    uint64_t time_ns_from_now = convert_cycles_to_ns(cycles_from_now, OP_freq_khz);
    OP_add_event(id, time_ns_from_now, info);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class OPERABLE_TYPE> OPERABLE_TYPE*
arbitrate_event_selection_from_vector(std::vector<OPERABLE_TYPE*>& operables)
{
    OPERABLE_TYPE* m = operables[0];
    for (size_t i = 1; i < operables.size(); i++)
    {
        if (!m->has_event())
            m = operables[i];
        else if (operables[i]->has_event() && operables[i]->next_event().time_ns < m->next_event().time_ns)
            m = operables[i];
    }
    return m;
}

template <class OP1, class OP2, class... OPERABLES> bool
arbitrate_event_execution(OP1 champion, OP2 challenger, OPERABLES... remaining)
{
    // if the challenger's event is earlier than the champion's event, then the challenger is the new champion
    if (!champion->has_event() || (challenger->has_event() && challenger->next_event().time_ns < champion->next_event().time_ns))
        return arbitrate_event_execution(challenger, remaining...);
    else
        return arbitrate_event_execution(champion, remaining...);
}

template <class OP1, class OP2> bool
arbitrate_event_execution(OP1 champion, OP2 challenger)
{
    // if the challenger's event is earlier than the champion's event, then the challenger is the new champion
    if (challenger->has_event() && (!champion->has_event() || challenger->next_event().time_ns < champion->next_event().time_ns))
    {
        auto e = challenger->next_event();
        challenger->pop_event();
        challenger->OP_process_event(e);
    }
    else if (champion->has_event())
    {
        auto e = champion->next_event();
        champion->pop_event();
        champion->OP_process_event(e);
    }
    else
    {
        return true;
    }
    return false;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#undef TEMPL_PARAMS
#undef TEMPL_CLASS