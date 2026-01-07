/*
 *  author: Suhas Vittal
 *  date:   6 January 2026
 * */

#ifndef SIM_OPERABLE_h
#define SIM_OPERABLE_h

#include "globals.h"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

extern cycle_type GL_MAX_CYCLES_WITH_NO_PROGRESS;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class OPERABLE
{
public:
    const std::string name;
    const double      freq_khz;
private:
    cycle_type   current_cycle_{0};

    /*
     * Since this is a cycle-level simulation,
     * we need to `"skip" cycles to account
     * for the differences in speeds of
     * different component.
     *
     * When `leap < 1.0`, we execute a cycle.
     * Then, we increment `leap_` by `clock_scale_`.
     *
     * `clock_scale_ = <fastest_freq_khz>/<this_freq_khz>`
     *
     * See `coordinate_clock_scale` to see where this
     * is set.
     * */
    double leap_{0.0};
    double clock_scale_;

    cycle_type cycles_with_no_progress_{0};
public:
    OPERABLE(std::string_view name, double freq_khz);

    /*
     * `tick()` calls `operate()` (see below)
     * and increments `current_cycle_` if
     * `leap_ < 1.0`. Otherwise, `leap_` is incremented
     * by `clock_scale_`.
     * */
    void tick();

    /*
     * Logging functions:
     * */
    virtual void print_progress(std::ostream&) const {}
    virtual void print_deadlock_info(std::ostream&) const {}

    cycle_type current_cycle() const;
protected:
    /* 
     * This is what the descendant should implement.
     * `operate()` should return the *amount* of progress
     * done.
     * */
    virtual long operate() =0;
private:
    friend void coordinate_clock_scale(std::vector<OPERABLE*>);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Sets the clock scale of all components passed in.
 * */
void coordinate_clock_scale(std::vector<OPERABLE*>);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#endif   // SIM_OPERABLE_h
