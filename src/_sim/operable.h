/*
    author: Suhas Vittal
    date:   15 September 2025

    Inspired by champsim's `operable.h`.
*/

#ifndef SIM_OPERABLE_h
#define SIM_OPERABLE_h

#include "sim/clock.h"

#include <cstdint>
#include <queue>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
    declaration of `EVENT` type and its specialization
    for `std::less`
*/

namespace sim
{

template <class ID_TYPE, class INFO_TYPE>
struct EVENT
{
    ID_TYPE id;
    uint64_t time_ns;
    INFO_TYPE info;
};

}   // namespace sim

namespace std
{

template <class ID_TYPE, class INFO_TYPE>
struct less<sim::EVENT<ID_TYPE, INFO_TYPE>>
{
    using value_type = sim::EVENT<ID_TYPE, INFO_TYPE>;

    // needs to be reversed to make `std::priority_queue` a min-heap
    bool
    operator()(const value_type& x, const value_type& y) const
    {
        return x.time_ns > y.time_ns;
    }
};

}   // namespace std

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// this is a placeholder for event info that is not needed
struct NO_EVENT_INFO {};

// this is the global time
extern uint64_t GL_CURRENT_TIME_NS;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// prefix all `OPERABLE` fields and functions with `OP_` so it is clear
// that they are part of the `OPERABLE` class:
template <class EVENT_ID_TYPE, class EVENT_INFO_TYPE>
class OPERABLE
{
public:
    using event_type = EVENT<EVENT_ID_TYPE, EVENT_INFO_TYPE>;

    // clock frequency in kilo hertz
    const double OP_freq_khz;
private:
    std::priority_queue<event_type> OP_event_queue;
public:
    OPERABLE(double freq_khz);

    // exposes `OP_event_queue` to user:
    size_t     num_events() const { return OP_event_queue.size(); }
    bool       has_event() const { return !OP_event_queue.empty(); }
    event_type next_event() const;
    void       pop_event();

    // This will execute the given event by called `OP_handle_event`.
    // Before `OP_handle_event` is called, any affected variables are updated.
    virtual void OP_process_event(event_type);

    // this is the first call to any `OPERABLE` subclass:
    virtual void OP_init() =0;

    // function for adding events to `OP_event_queue`:
    // it will create an `EVENT` based on the given parameters and add it to `OP_event_queue`:
    void OP_add_event(EVENT_ID_TYPE, uint64_t time_ns_from_now, EVENT_INFO_TYPE=EVENT_INFO_TYPE{});
    void OP_add_event_using_cycles(EVENT_ID_TYPE, uint64_t cycles_from_now, EVENT_INFO_TYPE=EVENT_INFO_TYPE{});

    uint64_t current_cycle() const { return convert_ns_to_cycles(GL_CURRENT_TIME_NS, OP_freq_khz); }
protected:

    // implements each event:
    virtual void OP_handle_event(event_type) =0;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// `arbitrate_event_selection_from_vector` is used to select the operable with the earliest event from a vector of operables
// these operables must all descend from a common `OPERABLE` instance
template <class OPERABLE_TYPE> OPERABLE_TYPE* arbitrate_event_selection_from_vector(std::vector<OPERABLE_TYPE*>& operables);

// `arbitrate_event_execution` is used to arbitrate the execution of events between multiple operables
// returns true if a deadlock is detected (no events to process)
template <class OP1, class OP2, class... OPERABLES> bool arbitrate_event_execution(OP1, OP2, OPERABLES... remaining);
template <class OP1, class OP2> bool                     arbitrate_event_execution(OP1, OP2);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#include "sim/operable.tpp"

#endif  // SIM_OPERABLE_h