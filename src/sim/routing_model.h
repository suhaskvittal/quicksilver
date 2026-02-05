/*
 *  author: Suhas Vittal
 *  date:   5 February 2026
 * */

#ifndef SIM_ROUTING_MODEL_h
#define SIM_ROUTING_MODEL_h

#include "globals.h"

#include <string>
#include <string_view>
#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T>
class ROUTING_MODEL
{
protected:
    const std::vector<T*> entities_;
public:
    ROUTING_MODEL(std::vector<T*> entities)
        :entities_(entities)
    {}

    virtual bool can_route_to(T*, cycle_type current_cycle) const =0;
    virtual void lock_route_to(T*, cycle_type until_cycle) =0;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#endif // SIM_ROUTING_MODEL_h
