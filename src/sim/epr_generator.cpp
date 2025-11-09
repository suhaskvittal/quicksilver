/*
    author: Suhas Vittal
    date:   7 October 2025
*/

#include "sim/epr_generator.h"
#include "sim/memory.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

EPR_GENERATOR::EPR_GENERATOR(double freq_khz, MEMORY_MODULE* owner, size_t buffer_cap)
    :OPERABLE(freq_khz),
    owner_(owner),
    buffer_capacity_(buffer_cap)
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
EPR_GENERATOR::consume_epr_pairs(size_t count)
{
    if (epr_buffer_occu_ < count)
        throw std::runtime_error("EPR_GENERATOR::consume_epr_pairs: attempting to consume more than available");
    epr_buffer_occu_ -= count;
    OP_add_event_using_cycles(EG_EVENT_TYPE::EPR_CONSUMED, 0, EG_EVENT_INFO{});
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
EPR_GENERATOR::OP_init()
{
    OP_add_event_using_cycles(EG_EVENT_TYPE::EPR_GENERATED, 1, EG_EVENT_INFO{});
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
EPR_GENERATOR::OP_handle_event(event_type event)
{
    if (event.id == EG_EVENT_TYPE::EPR_GENERATED)
    {
        epr_buffer_occu_++;
        if (epr_buffer_occu_ < buffer_capacity_)
            OP_add_event_using_cycles(EG_EVENT_TYPE::EPR_GENERATED, 1, EG_EVENT_INFO{});

        owner_->OP_add_event(MEMORY_EVENT_TYPE::RETRY_MEMORY_ACCESS, 0);
    }
    else if (event.id == EG_EVENT_TYPE::EPR_CONSUMED)
    {
        OP_add_event_using_cycles(EG_EVENT_TYPE::EPR_GENERATED, 1, EG_EVENT_INFO{});
    }
    else
    {
        throw std::runtime_error("EPR_GENERATOR::OP_handle_event: unexpected event type: " 
                                    + std::to_string(static_cast<size_t>(event.id)));
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim
