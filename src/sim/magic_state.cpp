/*
    author: Suhas Vittal
    date:   26 August 2025
*/

#include "sim/magic_state.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <random>

extern std::mt19937 GL_RNG;
static std::uniform_real_distribution<double> FP_RAND{0.0, 1.0};

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

T_FACTORY::T_FACTORY(
    double _freq_khz,
    double _output_error_prob, 
    size_t _initial_input_count, 
    size_t _output_count, 
    size_t _num_rotation_steps,
    size_t _buffer_capacity,
    ssize_t _output_patch_idx,
    size_t _level)
    : freq_khz(_freq_khz),
      output_error_prob(_output_error_prob),
      initial_input_count(_initial_input_count),
      output_count(_output_count),
      num_rotation_steps(_num_rotation_steps),
      buffer_capacity(_buffer_capacity),
      output_patch_idx(_output_patch_idx),
      level(_level)
{}

/*
    Implementations are based on "Magic State Distillation: Not as costly as you think" (Litinski)
*/

T_FACTORY
T_FACTORY::f15to1(size_t level_preset, uint64_t t_round_ns, size_t buffer_capacity, ssize_t output_patch_idx)
{
    double freq_khz;
    double error_prob;

    double t_round_ms = static_cast<double>(t_round_ns) * 1e-6;
    if (level_preset <= 1)
    {
        freq_khz = 1.0 / (5 * t_round_ms);  // cycle is 5 rounds
        error_prob = 1e-6;  // don't have exact numbers for this, but should be around here since d = 7 gives 4.5e-8
    }
    else
    {
        freq_khz = 1.0 / (11 * t_round_ms);
        error_prob = 2.7e-12;
    }

    return T_FACTORY(freq_khz, error_prob, 4, 1, 11, buffer_capacity, output_patch_idx, level_preset);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
T_FACTORY::tick()
{
    if (buffer_occu >= buffer_capacity)
        return;

    double step_ok_prob;
    if (step == 0)
    {
        // check if we have enough resources to start the factory
        double input_error_prob;
        if (level == 0)
        {
            // Then, assume this is a first level factory -- we always have enough:
            input_error_prob = INJECTED_STATE_FAILURE_PROB;
        }
        else
        {
            size_t resources_avail = std::transform_reduce(resource_producers.begin(), resource_producers.end(),
                                                        size_t{0},
                                                        std::plus<size_t>{},
                                                        [] (auto* f_p)
                                                        {
                                                            return f_p->buffer_occu;
                                                        });

            if (resources_avail < initial_input_count)
                return;
            size_t required_resources{initial_input_count};
            // take resources in a round robin until we have enough.
            double tot_error_prob{0.0};  // because I am lazy, just take the average of all producers
            while (required_resources > 0)
            {
                for (size_t i = 0; i < resource_producers.size() && required_resources > 0; i++)
                {
                    auto* f_p = resource_producers[i];
                    if (f_p->buffer_occu > 0)
                    {
                        f_p->buffer_occu--;
                        required_resources--;
                        tot_error_prob += f_p->output_error_prob;
                    }
                }
            }
            input_error_prob = tot_error_prob / static_cast<double>(initial_input_count);
        }

        // simulate an error -- as a first order approximation, we will assume that if any error occurs,
        // the factory fails. Note that this does not account for undetectable errors, so the factory
        // will fail slightly more often than in reality (but since undetectable errors are like p^3 or higher,
        // this is a good approximation).

        step_ok_prob = 1.0 - initial_input_count * input_error_prob;
    }
    else 
    {
        // get one resource state
        if (level == 0)
        {
            step_ok_prob = 1.0 - INJECTED_STATE_FAILURE_PROB;
        }
        else
        {
            auto prod_it = std::find_if(resource_producers.begin(), resource_producers.end(),
                                        [] (auto* f_p) { return f_p->buffer_occu > 0; });
            if (prod_it == resource_producers.end())
                return;
            auto* f_p = *prod_it;
            f_p->buffer_occu--;
            step_ok_prob = 1.0 - f_p->output_error_prob;
        }
    }

    if (FP_RAND(GL_RNG) < step_ok_prob)
    {
        step++;
        if (step == 1+num_rotation_steps)
        {
            // factory is done -- add to buffer
            buffer_occu++;
            s_prod_tries++;
            step = 0;
        }
    }
    else
    {
        // need to reset:
        step = 0;
        s_failures++;
        s_prod_tries++;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim