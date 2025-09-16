/*
    author: Suhas Vittal
    date:   15 September 2025

    Inspired by champsim's `operable.h`.
*/

#ifndef SIM_OPERABLE_h
#define SIM_OPERABLE_h

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

    bool
    operator()(const value_type& x, const value_type& y) const
    {
        return x.time_ns < y.time_ns;
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

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class EVENT_ID_TYPE, class EVENT_INFO_TYPE>
class OPERABLE
{
public:
    using event_type = EVENT<EVENT_ID_TYPE, EVENT_INFO_TYPE>;

    // clock frequency in kilo hertz
    const double OP_freq_khz;
protected:
    uint64_t OP_current_cycle{0};
private:
    // prefix all `OPERABLE` fields and functions with `OP_` so it is clear
    // that they are part of the `OPERABLE` class:
    std::priority_queue<event_type> OP_event_queue;
public:
    OPERABLE(double freq_khz);

    // exposes `OP_event_queue` to user:
    event_type next_event() const;
    void       pop_event();

    // This will execute the given event by called `OP_handle_event`.
    // Before `OP_handle_event` is called, any affected variables are updated.
    virtual void OP_process_event(event_type);

    // this is the first call to any `OPERABLE` subclass:
    virtual void init() =0;
protected:
    // internal function for subclass to add events to `OP_event_queue`:
    // it will create an `EVENT` based on the given parameters and add it to `OP_event_queue`:
    virtual void OP_add_event(ID_TYPE, uint64_t cycles_from_now, INFO_TYPE) =0;
    
    // implements each event:
    virtual void OP_handle_event(event_type) =0;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#include "sim/operable.tpp"

#endif  // SIM_OPERABLE_h