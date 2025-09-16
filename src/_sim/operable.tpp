/*
    author: Suhas Vittal
    date:   15 September 2025
*/

#include "sim/clock.h"

#include <cmath>

#define TEMPL_PARAMS    template <class EVENT_ID_TYPE, class EVENT_INFO_TYPE>
#define TEMPL_CLASS     OPERABLE<EVENT_ID_TYPE, EVENT_INFO_TYPE>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS 
TEMPL_CLASS::OPERABLE(double freq_khz) 
    :OP_freq_khz(freq_khz)
{
    init();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS 
TEMPL_CLASS::next_event() const
{
    return OP_event_queue.top();
}

TEMPL_PARAMS 
TEMPL_CLASS::pop_event()
{
    OP_event_queue.pop();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS 
TEMPL_CLASS::OP_process_event(event_type event)
{
    // update state:
    OP_current_cycle = compute_cycles_from_ns(event.time_ns, OP_freq_khz);

    // call subclass's implementation of the event:
    OP_handle_event(event);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#undef TEMPL_PARAMS
#undef TEMPL_CLASS

#endif  // SIM_OPERABLE_h