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
    const size_t initial_input_count_;
    const size_t output_count_;
    const size_t num_rotation_steps_;
    const size_t buffer_capacity_;
    const size_t level_;

    // set by `COMPUTE` during initialization
    ssize_t output_patch_idx_{-1};

    // number of produced states -- cannot produce if `buffer_occu >= buffer_capacity`
    size_t buffer_occu_{0};

    std::vector<T_FACTORY*> previous_level_{};
    std::vector<T_FACTORY*> next_level_{};

    // stats:
    uint64_t s_prod_tries_{0};
    uint64_t s_failures_{0};
private:
    // `step` tracks the progress of the factory. Since, there are `1 + num_rotation_steps` 
    // steps before the factory is done, `0 <= step < 1 + num_rotation_steps`. `step` is only
    // used if the factory is not a first level factory, as we can directly compute how long
    // the factory will take to complete if it is a first level factory.
    size_t step_{0};

    // we need this to track the last production cycle to avoid calling `production_step` multiple
    // times in the same cycle
    int64_t last_production_cycle_{-1};
public:
    T_FACTORY(double freq_khz, 
                double output_error_prob, 
                size_t initial_input_count,
                size_t output_count,
                size_t num_rotation_steps,
                size_t buffer_capacity,
                size_t level);

    void OP_init() override;
    void OP_handle_event(event_type) override;

    void consume_state(size_t num_consumed=1);

    size_t get_step() const { return step_; }
private:
    void production_step();
    void production_step_level_0();
    void production_step_level_1_step_0();
    void production_step_level_1_step_x();
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif
