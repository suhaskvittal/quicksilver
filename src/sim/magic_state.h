/*
    author: Suhas Vittal
    date:   26 August 2025
*/

#ifndef SIM_MAGIC_STATE_h
#define SIM_MAGIC_STATE_h

#include <cstddef>
#include <random>

#include <sys/types.h>

namespace sim
{

////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////

const double INJECTED_STATE_FAILURE_PROB{1e-3};

struct T_FACTORY
{
    /*
        Factory parameters:
    */
    const double freq_khz;
    const double output_error_prob;
    const size_t initial_input_count;
    const size_t output_count;
    const size_t num_rotation_steps;
    const size_t buffer_capacity;
    const ssize_t output_patch_idx;
    const size_t level;

    // `step` tracks the progress of the factory. Since, there are `1 + num_rotation_steps` 
    // steps before the factory is done, `0 <= step < 1 + num_rotation_steps`.
    size_t step{0};
    size_t buffer_occu{0};

    // `resource_producers` are the factories that produce the resource states for this factory.
    // If this a first level factory, then `resource_producers` is empty. Otherwise, it is a vector
    // of pointers to locations where we can look for resource states.
    std::vector<T_FACTORY*> resource_producers;

    T_FACTORY(
        double freq_khz,
        double output_error_prob, 
        size_t initial_input_count, 
        size_t output_count, 
        size_t num_rotation_steps,
        size_t buffer_capacity,
        ssize_t output_patch_idx,
        size_t level);

    static T_FACTORY f15to1(size_t level_preset, uint64_t t_round_ns, 
                                        size_t buffer_capacity, ssize_t output_patch_idx);

    void tick();
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

 #endif // SIM_MAGIC_STATE_h