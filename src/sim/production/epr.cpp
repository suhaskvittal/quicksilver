/*
 *  author: Suhas Vittal
 *  date:   18 February 2026
 * */

#include "sim/production/epr.h"

#include <random>

namespace sim
{

extern std::mt19937_64 GL_RNG;

namespace producer
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

const double INJECTION_ERROR_PROB{0.01};  // 1% initial EPR error rate
static std::uniform_real_distribution FPR(0.0,1.0);

/*
 * Generate name for entanglement distillation protocol: "E_<input_count>_<output_count>"
 * */
std::string _ed_name(size_t input_count, size_t output_count);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ENT_DISTILLATION::ENT_DISTILLATION(double freq_khz,
                                    double output_error_prob,
                                    size_t buffer_capacity,
                                    size_t _input_count,
                                    size_t _output_count,
                                    size_t _num_checks)
    :PRODUCER_BASE(_ed_name(_input_count, _output_count), freq_khz, output_error_prob, buffer_capacity),
    input_count(_input_count),
    output_count(_output_count),
    num_checks(_num_checks)
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
ENT_DISTILLATION::production_step()
{
    // if this is the first level, assume all inputs are already available
    if (step_ == 0 && previous_level.empty())
    {
        inputs_available_ = input_count;
        error_probability_ = input_count * INJECTION_ERROR_PROB;
    }

    // fetch EPR pairs from the previous level of production
    if (step_ == 0 && inputs_available_ < input_count)
    {
        if (awaiting_input_)
        {
            inputs_available_++;
            awaiting_input_ = false;
        }
        else
        {
            // check if a previous level has an available EPR pair to provide:
            auto p_it = std::find_if(previous_level.begin(), previous_level.end(),
                                [] (const auto* p) { return p->buffer_occupancy() > 0; });
            if (p_it == previous_level.end())
                return false;
            (*p_it)->consume(1);
            awaiting_input_ = true;
            error_probability_ += (*p_it)->output_error_probability;
        }
    }
    else
    {
        // can start doing checks:
        step_++;
        if (step_ == num_checks)
        {
            // check if an error occurred -- only install if it does not
            if (GL_RNG() > error_probability_)
                install_resource_state();

            step_ = 0;
            inputs_available_ = 0;
            awaiting_input_ = false;
            error_probability_ = 0.0;
        }
    }

    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

std::string
_ed_name(size_t i, size_t o)
{
    return "E_" + std::to_string(i) + "_" + std::to_string(o);
}


} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace producer
} // namespace sim
