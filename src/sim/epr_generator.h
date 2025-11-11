/*
    author: Suhas Vittal
    date:   7 October 2025
*/

#ifndef SIM_EPR_GENERATOR_h
#define SIM_EPR_GENERATOR_h

#include "sim/operable.h"
#include "sim/client.h"  // for QUBIT type

#include <deque>

namespace sim
{

class MEMORY_MODULE;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

enum class EG_EVENT_TYPE
{
    EPR_GENERATED
};

struct EG_EVENT_INFO
{
};

/*
 * This implements an EPR pair generator.
 * */
class EPR_GENERATOR : public OPERABLE<EG_EVENT_TYPE, EG_EVENT_INFO>
{
public:
    using typename OPERABLE<EG_EVENT_TYPE, EG_EVENT_INFO>::event_type;

    const size_t buffer_capacity_;
    const size_t max_decoupled_loads_;
private:
    size_t             epr_buffer_occu_{0};
    std::deque<QUBIT>  decoupled_loads_{};

    size_t inflight_decoupled_loads_{0};

    MEMORY_MODULE* owner_;

    bool has_inflight_epr_generation_event_{false};
public:
    EPR_GENERATOR(double freq_khz, MEMORY_MODULE*, size_t buffer_cap);

    void OP_init() override;

    void consume_epr_pairs(size_t count);
    void alloc_decoupled_load(QUBIT);
    void exchange_decoupled_load(QUBIT, QUBIT);
    void remove_decoupled_load(QUBIT);
    QUBIT free_decoupled_load();

    bool contains_loaded_qubit(QUBIT) const;

    void mark_inflight_decoupled_load();
    size_t get_occupancy() const;
    bool has_capacity() const;
    bool can_store_decoupled_load() const;

    void dump_deadlock_info();

    const std::deque<QUBIT>& get_decoupled_loads() const { return decoupled_loads_; }
protected:
    void OP_handle_event(event_type) override;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif  // SIM_EPR_GENERATOR_h
