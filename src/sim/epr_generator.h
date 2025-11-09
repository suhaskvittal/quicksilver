/*
    author: Suhas Vittal
    date:   7 October 2025
*/

#ifndef SIM_EPR_GENERATOR_h
#define SIM_EPR_GENERATOR_h

#include "sim/operable.h"
#include "sim/client.h"  // for QUBIT type

namespace sim
{

class MEMORY_MODULE;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

enum class EG_EVENT_TYPE
{
    EPR_GENERATED,
    EPR_CONSUMED,

    DECOUPLED_LOAD_ALLOC,
    DECOUPLED_LOAD_FREE
};

struct EG_EVENT_INFO
{
    // for decoupled load only;
    QUBIT loaded_qubit;
};

/*
 * This implements an EPR pair generator.
 * */
class EPR_GENERATOR : public OPERABLE<EG_EVENT_TYPE, EG_EVENT_INFO>
{
public:
    using typename OPERABLE<EG_EVENT_TYPE, EG_EVENT_INFO>::event_type;

    const size_t buffer_capacity_;
private:
    size_t epr_buffer_occu_{0};

    MEMORY_MODULE* owner_;
public:
    EPR_GENERATOR(double freq_khz, MEMORY_MODULE*, size_t buffer_cap);

    // Public interface for checking and consuming EPR pairs
    size_t get_occupancy() const { return epr_buffer_occu_; }
    void consume_epr_pairs(size_t count);

    void OP_init() override;
protected:
    void OP_handle_event(event_type) override;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif  // SIM_EPR_GENERATOR_h
