/*
 *  author: Suhas Vittal
 *  date:   6 January 2026
 * */

#ifndef SIM_OPERABLE_h
#define SIM_OPERABLE_h

#include "globals.h"
#include "sim.h"

#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class Operable
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
    Operable(std::string_view name, double freq_khz);

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

    cycle_type current_cycle() const { return current_cycle_; }
protected:
    /* 
     * This is what the descendant should implement.
     * `operate()` should return the *amount* of progress
     * done.
     * */
    virtual long operate() =0;
private:
    friend void coordinate_clock_scale(std::vector<Operable*>);
    friend void fast_forward_all_operables_to_time_ns(std::vector<Operable*>, uint64_t);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Computes the frequency (kHz) for the given period (ns)
 * */
inline double compute_freq_khz(uint64_t period_in_nanoseconds) { return 1e6 / static_cast<double>(period_in_nanoseconds); }

/*
 * Converts clock cycles between two different frequencies.
 * We make this templated so we can also convert means and
 * such.
 * */
template <class T>
T convert_cycles_between_frequencies(T, double original_freq_khz, double f2);

uint64_t convert_cycles_to_time_ns(cycle_type, double freq_khz);
cycle_type convert_time_ns_to_cycles(uint64_t, double freq_khz);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Sets the clock scale of all components passed in relative to the
 * fastest `Operable` in the container.
 * */
void coordinate_clock_scale(std::vector<Operable*>);

/*
 * Fast forwards the clock of all operables in the container to the given time.
 * */
void fast_forward_all_operables_to_time_ns(std::vector<Operable*>, uint64_t);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Implementation of `convert_cycles_between_frequencies()`
 * */
template <class T> T
convert_cycles_between_frequencies(T cycles, double f1, double f2)
{
    double out = cycles * (f2/f1);
    if constexpr (std::is_integral<T>::value)
        out = std::ceil(out);
    return static_cast<T>(out);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#endif   // SIM_OPERABLE_h
