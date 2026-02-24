/*
 *  author: Suhas Vittal
 *  date:   18 February 2026
 * */

#include "sim/production/epr.h"

#include <random>

namespace sim
{

extern std::mt19937_64 GL_RNG;
extern double          GL_PHYSICAL_ERROR_RATE;

namespace producer
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

static std::uniform_real_distribution FPR(0.0,1.0);

double _injection_error_probability();

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
                                    size_t input_count,
                                    size_t output_count,
                                    size_t _measurement_distance,
                                    size_t _num_checks)
    :PRODUCER_BASE(_ed_name(input_count, output_count), 
                            freq_khz, 
                            output_error_prob, 
                            buffer_capacity,
                            input_count,
                            output_count),
    measurement_distance(_measurement_distance),
    num_checks(_num_checks),
    inputs_available_(previous_level.empty() ? input_count : 0)
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

cycle_type
ENT_DISTILLATION::get_next_progression_cycle() const
{
    const cycle_type next_avail_cycle = std::max(cycle_available_, current_cycle()+1);

    // not waiting for resources from previous level
    if (step_ != 0 || inputs_available_ >= input_count || previous_level.empty())
        return next_avail_cycle;

    // compute min ready cycle across previous_level
    bool any_have_available_state{false};
    cycle_type previous_level_avail_cycle{next_avail_cycle};
    for (const PRODUCER_BASE* _p : previous_level)
    {
        const auto* p = static_cast<const ENT_DISTILLATION*>(_p);
        cycle_type c = p->get_next_progression_cycle();
        c = convert_cycles_between_frequencies(c, p->freq_khz, freq_khz);
        previous_level_avail_cycle = std::min(c, previous_level_avail_cycle);

        any_have_available_state |= p->buffer_occupancy() > 0;
    }
    return any_have_available_state ? next_avail_cycle : previous_level_avail_cycle;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
ENT_DISTILLATION::production_step()
{
    if (current_cycle() < cycle_available_)
        return true;

    if (step_ == num_checks)
    {
        // check if an error occurred -- only install if it does not
        if (FPR(GL_RNG) > error_probability_)
            install_resource_states();
        else
            s_failures++;
        s_production_attempts++;
        step_ = 0;
        inputs_available_ = 0;
        error_probability_ = 0.0;
    }

    // if this is the first level, assume all inputs are already available
    if (step_ == 0 && previous_level.empty())
    {
        inputs_available_ = input_count;
        error_probability_ = input_count * _injection_error_probability();
    }

    // fetch EPR pairs from the previous level of production
    if (step_ == 0 && inputs_available_ < input_count)
    {
        // check if a previous level has an available EPR pair to provide:
        while (inputs_available_ < input_count)
        {
            auto p_it = std::find_if(previous_level.begin(), previous_level.end(),
                                [] (const auto* p) { return p->buffer_occupancy() > 0; });
            if (p_it == previous_level.end())
                return false;
            auto* p = *p_it;
            size_t c = std::min(p->buffer_occupancy(), input_count - inputs_available_);
            p->consume(c);
            error_probability_ += c * p->output_error_probability;
            inputs_available_ += c;
        }
    }

    if (inputs_available_ >= input_count)
    {
        step_++;
        cycle_available_ = current_cycle() + measurement_distance;
    }

    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

double
_injection_error_probability()
{
    return 10 * GL_PHYSICAL_ERROR_RATE;
}

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
