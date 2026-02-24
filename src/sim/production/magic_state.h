/*
 *  author: Suhas Vittal
 *  date:   6 January 2026
 * */

#ifndef SIM_PRODUCTION_MAGIC_STATE_h
#define SIM_PRODUCTION_MAGIC_STATE_h

#include "sim/production.h"

namespace sim
{
namespace producer
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class T_DISTILLATION : public PRODUCER_BASE
{
public:
    /*
     * `measurement_distance` affects how many rounds are spent performing
     * one rotation (via PPM)
     * */
    const size_t measurement_distance;

    /*
     * How production works:
     *  1.  the factory must consume `initial_input_count` magic states
     *  2.  the factory then consumes one magic state per `num_rotation_steps`
     *  3.  if the factory does not fail, then it produces `output_count` higher fidelity magic states
     *        note:  `output_count` is defined in `production.h`
     * */
    const size_t initial_input_count;
    const size_t num_rotation_steps;
private:
    size_t step_{0};

    /*
     * The simulation cycle at which the current PPM measurement completes.
     * Only meaningful when step_ > 0.
     * */
    cycle_type cycle_available_{0};
public:
    T_DISTILLATION(double freq_khz,
                    double output_error_prob,
                    size_t buffer_capacity,
                    size_t initial_input_count,
                    size_t output_count,
                    size_t measurement_distance,
                    size_t num_rotation_steps);

    void print_deadlock_info(std::ostream&) const override;
private:
    bool production_step() override;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class T_CULTIVATION : public PRODUCER_BASE
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

}  // namespace producer
}  // namespace sim

#endif
