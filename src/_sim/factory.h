/*
    author: Suhas Vittal
    date:   15 September 2025
*/

#ifndef SIM_FACTORY_h
#define SIM_FACTORY_h

#include "sim/operable.h"

namespace sim
{

const double INJECTED_STATE_FAILURE_PROB{1e-3};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

enum class FACTORY_EVENT_TYPE
{
    MAGIC_STATE_PRODUCED,
    STEP_PRODUCTION
};

class T_FACTORY : public OPERABLE<FACTORY_EVENT_TYPE, NO_EVENT_INFO>
{
public:
    using typename OPERABLE<FACTORY_EVENT_TYPE, NO_EVENT_INFO>::event_type;

    const double output_error_prob_;
    const size_t buffer_capacity_;
    const size_t level_;

    // need to expose these publicly since they are set by other objects
    ssize_t output_patch_idx_{-1};
    std::vector<T_FACTORY*> previous_level_{};
    std::vector<T_FACTORY*> next_level_{};

    // stats:
    uint64_t s_prod_tries{0};
    uint64_t s_failures{0};
protected:
    size_t buffer_occu_{0};

    // we need this to track the last production cycle to avoid calling `production_step` multiple
    // times in the same cycle
    int64_t last_production_cycle_{-1};
public:
    T_FACTORY(double freq_khz,
                double output_error_prob,
                size_t buffer_capacity,
                size_t level);

    void OP_init() override;
    void OP_handle_event(event_type) override;

    void consume_state(size_t num_consumed=1);

    virtual size_t get_step() const { return 0; }
    size_t get_occupancy() const { return buffer_occu_; }
protected:
    virtual void production_step() =0;

    bool can_do_step() const { return static_cast<int64_t>(current_cycle()) >= last_production_cycle_; }
    void update_last_production_cycle() { last_production_cycle_ = static_cast<int64_t>(current_cycle()); }
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class T_CULTIVATION : public T_FACTORY
{
public:
    using T_FACTORY::event_type;

    const double probability_of_success_;
public:
    T_CULTIVATION(double freq_khz, 
                    double output_error_prob, 
                    double probability_of_success,
                    size_t buffer_capacity,
                    size_t level);
private:
    void production_step() override;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class T_DISTILLATION : public T_FACTORY
{
public:
    using T_FACTORY::event_type;

    const size_t initial_input_count_;
    const size_t output_count_;
    const size_t num_rotation_steps_;
private:
    // `step` tracks the progress of the factory. Since, there are `1 + num_rotation_steps` 
    // steps before the factory is done, `0 <= step < 1 + num_rotation_steps`. `step` is only
    // used if the factory is not a first level factory, as we can directly compute how long
    // the factory will take to complete if it is a first level factory.
    size_t step_{0};
public:
    T_DISTILLATION(double freq_khz, 
                    double output_error_prob, 
                    size_t initial_input_count,
                    size_t output_count,
                    size_t num_rotation_steps,
                    size_t buffer_capacity,
                    size_t level);

    size_t get_step() const override { return step_; }
private:
    void production_step() override;
    void production_step_level_0();
    void production_step_level_1_step_0();
    void production_step_level_1_step_x();
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif
