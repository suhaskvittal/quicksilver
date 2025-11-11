/*
    author: Suhas Vittal
    date:   16 September 2025
*/

#include "sim/factory.h"
#include "factory.h"
#include "sim/compute.h"

#include <random>

#include <random>

namespace sim
{

extern COMPUTE* GL_CMP;
extern std::mt19937 GL_RNG;
extern std::uniform_real_distribution<double> FP_RAND;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

T_FACTORY::T_FACTORY(double freq_khz, 
                    double output_error_prob, 
                    size_t initial_input_count,
                    size_t output_count,
                    size_t num_rotation_steps,
                    size_t buffer_capacity,
                    size_t level)
    :OPERABLE(freq_khz),
     output_error_prob_(output_error_prob),
     initial_input_count_(initial_input_count),
     output_count_(output_count),
     num_rotation_steps_(num_rotation_steps),
     buffer_capacity_(buffer_capacity),
     level_(level)
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
T_FACTORY::OP_init()
{
    production_step();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
T_FACTORY::OP_handle_event(event_type event)
{
    switch (event.id)
    {
    case FACTORY_EVENT_TYPE::MAGIC_STATE_PRODUCED:
        buffer_occu_++;
        
        // notify next level factories or `GL_CMP` that a magic state is available
        if (next_level_.empty())
        {
            GL_CMP->OP_add_event(COMPUTE_EVENT_TYPE::MAGIC_STATE_AVAIL, 0);
        }
        else
        {
            for (auto* f_p : next_level_)
            {
                if (f_p->buffer_occu_ < f_p->buffer_capacity_)
                    f_p->OP_add_event(FACTORY_EVENT_TYPE::STEP_PRODUCTION, 0);
            }
        }

        if (buffer_occu_ < buffer_capacity_)
            production_step();
        break;
    case FACTORY_EVENT_TYPE::STEP_PRODUCTION:
        production_step();
        break;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
T_FACTORY::consume_state(size_t num_consumed)
{
    if (buffer_occu_ < num_consumed)
        throw std::runtime_error("L" + std::to_string(level_) + " factory had resource consumed with no buffer occupancy");

    bool buffer_was_full = (buffer_occu_ >= buffer_capacity_);
    buffer_occu_ -= num_consumed;

    if (buffer_was_full && buffer_occu_ < buffer_capacity_)
        OP_add_event_using_cycles(FACTORY_EVENT_TYPE::STEP_PRODUCTION, 1);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
T_FACTORY::production_step()
{
    // avoid calling `production_step` multiple times in the same cycle
    int64_t i_curr_cycle = static_cast<int64_t>(current_cycle());
    if (i_curr_cycle < last_production_cycle_)
        return;

    if (level_ == 0)
        production_step_level_0();
    else if (step_ == 0) // upper level factory, step 0 (initializing the qubits)
        production_step_level_1_step_0();
    else                // upper level factory, rotation step
        production_step_level_1_step_x();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
T_FACTORY::production_step_level_0()
{
    // as this is a first level factory, we can predict when the magic state will
    // be produced as we have no dependences
    uint64_t t_until_done = 1 + num_rotation_steps_;

    // `prob_fail` is the probability that the factory will fail before it is done
    s_prod_tries_++;
    double prob_fail = static_cast<double>(initial_input_count_+num_rotation_steps_) * INJECTED_STATE_FAILURE_PROB;
    while (FP_RAND(GL_RNG) < prob_fail)
    {
        s_prod_tries_++;
        s_failures_++;
        t_until_done += (1+num_rotation_steps_) / 2;  // assume that the factory will fail in the middle (on average)
    }

    OP_add_event_using_cycles(FACTORY_EVENT_TYPE::MAGIC_STATE_PRODUCED, t_until_done);
    
    // need to increment by `t_until_done` to avoid calling `production_step` while the state is being produced
    last_production_cycle_ = static_cast<int64_t>(current_cycle() + t_until_done);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
T_FACTORY::production_step_level_1_step_0()
{
    s_prod_tries_++;

    size_t total_resources_avail = std::transform_reduce(previous_level_.begin(), previous_level_.end(), 
                                                    size_t{0},
                                                    std::plus<size_t>(),
                                                    [] (auto* f) { return f->buffer_occu_; });

    if (total_resources_avail < initial_input_count_)
        return;

    // consume in a round robin manner
    size_t required_resources = initial_input_count_;
    double prob_fail = 0.0;
    while (required_resources > 0)
    {
        for (size_t i = 0; i < previous_level_.size() && required_resources > 0; i++)
        {
            auto* f = previous_level_[i];
            if (f->buffer_occu_ > 0)
            {
                required_resources--;
                f->consume_state();
                prob_fail += f->output_error_prob_;
            }
        }
    }

    // check if the factory fails:
    if (FP_RAND(GL_RNG) < prob_fail)
    {
        s_failures_++;
        step_ = 0;
    }
    else
    {
        step_++;
    }

    OP_add_event_using_cycles(FACTORY_EVENT_TYPE::STEP_PRODUCTION, 1);
    last_production_cycle_ = static_cast<int64_t>(current_cycle() + 1);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
T_FACTORY::production_step_level_1_step_x()
{
    auto f_it = std::find_if(previous_level_.begin(), previous_level_.end(),
                            [] (auto* f_p) { return f_p->buffer_occu_ > 0; });

    if (f_it != previous_level_.end())
    {
        T_FACTORY* f = *f_it;
        f->consume_state();

        // check if the factory fails:
        if (FP_RAND(GL_RNG) < f->output_error_prob_)
        {
            s_failures_++;
            step_ = 0;
            OP_add_event_using_cycles(FACTORY_EVENT_TYPE::STEP_PRODUCTION, 1);
        }
        else
        {
            step_++;
            if (step_ == 1+num_rotation_steps_)
            {
                step_ = 0;
                OP_add_event_using_cycles(FACTORY_EVENT_TYPE::MAGIC_STATE_PRODUCED, 1);
            }
            else
            {
                OP_add_event_using_cycles(FACTORY_EVENT_TYPE::STEP_PRODUCTION, 1);
            }
        }
        last_production_cycle_ = static_cast<int64_t>(current_cycle() + 1);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim
