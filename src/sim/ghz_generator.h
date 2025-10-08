/*
    author: Suhas Vittal
    date:   7 October 2025
*/

#ifndef SIM_GHZ_GENERATOR_h
#define SIM_GHZ_GENERATOR_h

#include "sim/operable.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

enum class GG_EVENT_TYPE
{
    GHZ_GENERATED,
    GHZ_CONSUMED,

    DECOUPLED_LOAD_ALLOC,
    DECOUPLED_LOAD_FREE
};

struct GG_EVENT_INFO
{
    // for decoupled load only;
    QUBIT loaded_qubit;
};

/*
 * This implements an EPR pair generator.
 *
 * An EPR pair generator is also needed for load-store
 * decoupling. The idea is that when a load is decoupled,
 * it occupies one of the EPR pairs on the compute side.
 * While the program qubit occupies the slot, an EPR
 * pair cannot take that slot.
 * */
class EPR_GENERATOR : public OPERABLE<GG_EVENT_TYPE, GG_EVENT_INFO>
{
public:
    using typename OPERABLE<GG_EVENT_TYPE, GG_EVENT_INFO>::event_type;

    const size_t buffer_capacity_;
private:
    size_t epr_buffer_occu_{0};

    // requirement : `decoupled_loads_.size() <= epr_buffer_occu_`
    std::vector<QUBIT> decoupled_loads_{};
public:
    EPR_GENERATOR(double freq_khz, size_t buffer_cap);

    // returns true if `q` is stored (decoupled load)
    bool is_buffering_qubit(QUBIT) const;
    // returns true if a decoupled load is possible (there is some EPR pair not tied to a buffered qubit)
    bool is_decoupled_load_possible() const;

    void OP_init() override;
private:
    void OP_handle_event(event_type) override;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif  // SIM_GHZ_GENERATOR_h
