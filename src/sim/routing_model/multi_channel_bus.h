#ifndef SIM_ROUTING_MODEL_MULTI_CHANNEL_BUS_h
#define SIM_ROUTING_MODEL_MULTI_CHANNEL_BUS_h

#include "sim/routing_model.h"

namespace sim
{
namespace routing
{

/*
 *            |----- BLOCK
 *            |----- BLOCK
 * CHANNEL 0  |----- BLOCK
 *            |----- BLOCK
 *            |----- BLOCK
 * ENTRY -----|
 *            |----- BLOCK
 *            |----- BLOCK
 * CHANNEL 1  |----- BLOCK
 *            |----- BLOCK
 *            |----- BLOCK
 *
 * Channels can be accessed concurrently (though beyond 2, the
 * layout will be different than the above).
 * */

template <class T>
class MULTI_CHANNEL_BUS : public ROUTING_MODEL<T>
{
public:
    const size_t num_channels;
protected:
    using ROUTING_MODEL<T>::entities_;
private:
    std::vector<cycle_type> cycle_available_;
public:
    MULTI_CHANNEL_BUS(std::vector<T*> entities, size_t _num_channels)
        :ROUTING_MODEL<T>(entities),
        num_channels(_num_channels),
        cycle_available_(_num_channels, 0)
    {}

    bool
    can_route_to(T* x, cycle_type current_cycle) const override
    {
        return cycle_available_[channel_idx(x)] <= current_cycle;
    }

    void
    lock_route_to(T* x, cycle_type until_cycle) override
    {
        cycle_available_[channel_idx(x)] = until_cycle;
    }
private:
    size_t
    channel_idx(T* x) const
    {
        auto it = std::find(entities_.begin(), entities_.end(), x);
        size_t idx = std::distance(entities_.begin(), it);
        return idx % num_channels;
    }
};


} // namespace routing
} // namespace sim

#endif
