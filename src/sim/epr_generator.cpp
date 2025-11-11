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
    buffer_capacity_(buffer_cap),
    max_decoupled_loads_(buffer_cap/2)
{}

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
EPR_GENERATOR::consume_epr_pairs(size_t count)
{
    if (epr_buffer_occu_ < count)
        throw std::runtime_error("EPR_GENERATOR::consume_epr_pairs: attempting to consume more than available");

    bool was_full = !has_capacity();
    epr_buffer_occu_ -= count;

    // to avoid issuing duplicate events, only issue a generation event if the buffer was full before the consumption
    if (was_full && has_capacity())
        OP_add_event_using_cycles(EG_EVENT_TYPE::EPR_GENERATED, 1, EG_EVENT_INFO{});
}

void
EPR_GENERATOR::alloc_decoupled_load(QUBIT q)
{
    if (!can_store_decoupled_load())
        throw std::runtime_error("EPR_GENERATOR::alloc_decoupled_load: attempting to allocate more than capacity");
    decoupled_loads_.push_back(q);

    consume_epr_pairs(1);
}

QUBIT
EPR_GENERATOR::free_decoupled_load()
{
    if (decoupled_loads_.empty())
        throw std::runtime_error("EPR_GENERATOR::free_decoupled_load: attempting to free non-existent decoupled load");

    QUBIT q = std::move(decoupled_loads_.front());
    decoupled_loads_.pop_front();

    consume_epr_pairs(1);

    return q;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
EPR_GENERATOR::contains_loaded_qubit(QUBIT q) const
{
    auto q_it = std::find(decoupled_loads_.begin(), decoupled_loads_.end(), q);
    return q_it != decoupled_loads_.end();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
EPR_GENERATOR::get_occupancy() const
{
    return epr_buffer_occu_;
}

bool
EPR_GENERATOR::has_capacity() const
{
    return epr_buffer_occu_ + decoupled_loads_.size() < buffer_capacity_;
}

bool
EPR_GENERATOR::can_store_decoupled_load() const
{
    return decoupled_loads_.size() < max_decoupled_loads_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
EPR_GENERATOR::OP_handle_event(event_type event)
{
    if (event.id == EG_EVENT_TYPE::EPR_GENERATED)
    {
        epr_buffer_occu_++;
        if (has_capacity())
            OP_add_event_using_cycles(EG_EVENT_TYPE::EPR_GENERATED, 1, EG_EVENT_INFO{});

        owner_->OP_add_event(MEMORY_EVENT_TYPE::RETRY_MEMORY_ACCESS, 0);
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
