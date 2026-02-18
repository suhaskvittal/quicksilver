/*
 *  author: Suhas Vittal
 *  date:   6 January 2026
 * */

#include "sim/production/magic_state.h"

#include <cassert>
#include <iomanip>
#include <random>
#include <sstream>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

extern std::mt19937_64 GL_RNG;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace producer
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Helper functions (implemented at bottom of the file)
 * */

namespace
{

/*
 * Needed for determining failures:
 * */
static std::uniform_real_distribution FPR(0.0, 1.0);

/*
 * Assumed injection error rate:
 * */
const double INJECTION_ERROR_PROBABILITY{1e-3};

/*
 * Generate name for T_DISTILLATION factory: "D_<initial_input_count+num_rotation_steps>_<output_count>"
 * */
std::string _distillation_name(size_t initial_input_count, size_t output_count, size_t num_rotation_steps);

/*
 * Generate name for T_CULTIVATION factory: "C_p=<probability in scientific notation>"
 * */
std::string _cultivation_name(double probability_of_success);

}  // anonymous namespace

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

T_DISTILLATION::T_DISTILLATION(double freq_khz,
                                double output_error_probability,
                                size_t buffer_capacity,
                                size_t _initial_input_count,
                                size_t _output_count,
                                size_t _num_rotation_steps)
    :PRODUCER_BASE(_distillation_name(_initial_input_count, _output_count, _num_rotation_steps),
                        freq_khz,
                        output_error_probability,
                        buffer_capacity),
    initial_input_count(_initial_input_count),
    output_count(_output_count),
    num_rotation_steps(_num_rotation_steps)
{}

void
T_DISTILLATION::print_deadlock_info(std::ostream& out) const
{
    out << name << ": buffer occupancy = " << buffer_occupancy_ << " of " << buffer_capacity
                << ", step: " << step_ << " of " << (1+num_rotation_steps)
                << "\n";
}

bool
T_DISTILLATION::production_step()
{
    const bool is_lowest_level = previous_level.empty();

    size_t magic_states_needed = (step_ == 0) ? initial_input_count : 1; 
    const double p_sampled = FPR(GL_RNG);

    // get the magic states that we need and compute the error probability of these magic states.
    double p_error{0.0};
    if (is_lowest_level)
    {
        // can get all magic states via injection -- check if an error occurs.
        // If so, then restart production
        p_error = INJECTION_ERROR_PROBABILITY * magic_states_needed;
    }
    else
    {
        size_t magic_states_avail = std::transform_reduce(previous_level.begin(), previous_level.end(),   
                                                            size_t{0},
                                                            std::plus<size_t>{},
                                                            [] (const auto* f) { return f->buffer_occupancy(); });
        if (magic_states_avail < magic_states_needed)
            return false;
        
        // get the magic states we need -- simultaneoulsy compute the probability of error
        for (auto* f : previous_level)
        {
            if (f->buffer_occupancy() == 0)
                continue;
            size_t count = std::min(f->buffer_occupancy(), magic_states_needed);
            f->consume(count);
            magic_states_needed -= count;
            p_error += f->output_error_probability * count;
            if (magic_states_needed == 0)
                break;
        }
    }

    bool error_occurred = (p_sampled < p_error);
    if (error_occurred)
    {
        step_ = 0;
        s_production_attempts++;
        s_failures++;
    }
    else
    {
        step_++;
        if (step_ == num_rotation_steps+1)
        {
            install_resource_state();
            step_ = 0;
            s_production_attempts++;
        }
    }
    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

T_CULTIVATION::T_CULTIVATION(double freq_khz,
                                double output_error_probability,
                                size_t buffer_capacity,
                                double _probability_of_success,
                                size_t _rounds)
    :PRODUCER_BASE(_cultivation_name(_probability_of_success),
                     freq_khz,
                     output_error_probability,
                     buffer_capacity),
    probability_of_success(_probability_of_success),
    rounds(_rounds)
{}

bool
T_CULTIVATION::production_step()
{
    if (step_ == 0)
    {
        // select failure round:
        std::uniform_int_distribution<size_t> urand{0, rounds-1};
        if (FPR(GL_RNG) > probability_of_success)
            failure_round_ = urand(GL_RNG);
        else
            failure_round_ = std::numeric_limits<size_t>::max();
    }

    if (step_ == failure_round_)
    {
        step_ = 0;
        s_production_attempts++;
        s_failures++;
    }
    else
    {
        step_++;
        if (step_ == rounds)
        {
            install_resource_state();
            step_ = 0;
            s_production_attempts++;
        }
    }
    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/* HELPER FUNCTIONS START HERE */

namespace
{

std::string
_distillation_name(size_t initial_input_count, size_t output_count, size_t num_rotation_steps)
{
    return "D_" + std::to_string(initial_input_count + num_rotation_steps) + "_" + std::to_string(output_count);
}

std::string
_cultivation_name(double probability_of_success)
{
    std::ostringstream oss;
    oss << "C_p=" << static_cast<int>(100*probability_of_success) << "%";
    return oss.str();
}

}  // anonymous namespace

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace producer
} // namespace sim
