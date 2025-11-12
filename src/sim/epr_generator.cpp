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

EPR_GENERATOR::EPR_GENERATOR(double freq_khz, std::vector<MEMORY_MODULE*> memory_modules, size_t buffer_cap)
    :OPERABLE(freq_khz),
    buffer_capacity_(buffer_cap),
    max_cacheable_stores_(buffer_cap / 2),
    memory_modules_(memory_modules)
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
EPR_GENERATOR::OP_init()
{
    has_inflight_epr_generation_event_ = true;
    OP_add_event_using_cycles(EG_EVENT_TYPE::EPR_GENERATED, 1, EG_EVENT_INFO{});
}

void
EPR_GENERATOR::set_memory_modules(std::vector<MEMORY_MODULE*> memory_modules)
{
    memory_modules_ = memory_modules;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
EPR_GENERATOR::consume_epr_pairs(size_t count)
{
    if (buffer_occu_ < count)
        throw std::runtime_error("EPR_GENERATOR::consume_epr_pairs: attempting to consume more than available");

    bool was_full = !has_capacity();
    buffer_occu_ -= count;

    // to avoid issuing duplicate events, only issue a generation event if the buffer was full before the consumption
    if (!has_inflight_epr_generation_event_ && has_capacity())
    {
        has_inflight_epr_generation_event_ = true;
        OP_add_event_using_cycles(EG_EVENT_TYPE::EPR_GENERATED, 1, EG_EVENT_INFO{});
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
EPR_GENERATOR::cache_qubit(QUBIT q)
{
    if (cached_qubits_.find(q) != cached_qubits_.end())
        throw std::runtime_error("EPR_GENERATOR::cache_qubit: qubit already cached -- " + q.to_string());
    cached_qubits_.insert(q);
}

void
EPR_GENERATOR::remove_qubit(QUBIT q)
{
    auto it = cached_qubits_.find(q);
    if (it == cached_qubits_.end())
        throw std::runtime_error("EPR_GENERATOR::remove_qubit: qubit not cached -- " + q.to_string());
    cached_qubits_.erase(it);
}

void
EPR_GENERATOR::swap_qubit_for(QUBIT old_q, QUBIT new_q)
{
    remove_qubit(old_q);
    cache_qubit(new_q);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
EPR_GENERATOR::dump_deadlock_info()
{
    std::cerr << "\tbuffer occu: " << buffer_occu_ << "\n";
    std::cerr << "\tcached stores: ";
    for (auto q : cached_qubits_)
        std::cerr << " " << q;
    std::cerr << "\n";
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
EPR_GENERATOR::OP_handle_event(event_type event)
{
    size_t bin_size = buffer_capacity_ < s_occu_hist.size() ? 1 : buffer_capacity_ / s_occu_hist.size();
    bin_size = std::min(bin_size, s_occu_hist.size()-1);
    s_occu_hist[buffer_occu_ / bin_size]++;

    if (event.id == EG_EVENT_TYPE::EPR_GENERATED)
    {
        buffer_occu_++;
        if (has_capacity())
            OP_add_event_using_cycles(EG_EVENT_TYPE::EPR_GENERATED, 1, EG_EVENT_INFO{});
        else
            has_inflight_epr_generation_event_ = false;

        // Notify all memory modules that EPR pairs are available
        for (auto* mem_module : memory_modules_)
        {
            mem_module->OP_add_event(MEMORY_EVENT_TYPE::RETRY_MEMORY_ACCESS, 0);
        }
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
