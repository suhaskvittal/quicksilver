/*
 *  author: Suhas Vittal
 *  date:   6 January 2026
 * */

#ifndef SIM_FACTORY_h
#define SIM_FACTORY_h

#include "globals.h"
#include "sim/operable.h"

#include <deque>
#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class T_FACTORY_BASE : public OPERABLE
{
public:
    const double output_error_probability;
    const size_t buffer_capacity;

    /*
     * Factories that produce magic states for this factory to use.
     * If empty, then it is assumed that this factory consumes
     * magic states created via state injection.
     * */
    std::vector<T_FACTORY_BASE*> previous_level;

    /*
     * Statistics:
     * */
    uint64_t s_production_attempts{0};
    uint64_t s_failures{0};
    uint64_t s_consumed{0};
    uint64_t s_total_buffer_lifetime{0};
protected:
    /*
     * Number of magic states in local buffer (max `buffer_capacity`)
     * */
    size_t buffer_occupancy_{0};

    /*
     * Cycle of magic state install
     * */
    std::deque<uint64_t> buffer_install_timestamp_{};
public:
    T_FACTORY_BASE(std::string_view name, 
                    double freq_khz, 
                    double output_error_prob,
                    size_t buffer_capacity);

    /*
     * Safely consumes `count` magic states from the buffer.
     * */
    void consume(size_t count);

    void print_deadlock_info(std::ostream&) const override;

    size_t buffer_occupancy() const;
protected:
    long operate() override;

    /*
     * Adds a magic state to the buffer.
     * */
    virtual void install_magic_state();

    /*
     * `operate()` calls `production_step`, which advances
     * magic state production by one cycle. Implementation
     * is defined by descendant class.
     *
     * This function should return true if anything was
     * attempted (regardless of failure).
     * */
    virtual bool production_step() =0;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class T_DISTILLATION : public T_FACTORY_BASE
{
public:
    /*
     * How production works:
     *  1.  the factory must consume `initial_input_count` magic states
     *  2.  the factory then consumes one magic state per `num_rotation_steps`
     *  3.  if the factory does not fail, then it produces `output_count` higher fidelity magic states
     * */
    const size_t initial_input_count;
    const size_t output_count;
    const size_t num_rotation_steps;
private:
    size_t step_{0};
public:
    T_DISTILLATION(double freq_khz,
                    double output_error_prob,
                    size_t buffer_capacity,
                    size_t initial_input_count,
                    size_t output_count,
                    size_t num_rotation_steps);

    void print_deadlock_info(std::ostream&) const override;
private:
    bool production_step() override;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class T_CULTIVATION : public T_FACTORY_BASE
{
public:
    /*
     * Note that `probability_of_success` corresponds to the 
     * probability that the protocol discards the state within
     * `rounds_with_possible_failure`. The failure round is
     * chosen a priori via URAND selection.
     *
     * Obviously, this is a very approximate implementation (see
     * the original paper for a plot of survival rate by round),
     * but good enough to have a basic model.
     * */
    const double probability_of_success;
    const size_t rounds;
private:
    size_t step_{0};
    size_t failure_round_;
public:
    T_CULTIVATION(double freq_khz,
                    double output_error_prob,
                    size_t buffer_capacity,
                    double probability_of_success,
                    size_t rounds);
private:
    bool production_step() override;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim

#endif  // SIM_FACTORY_h
