/*
    author: Suhas Vittal
    date:   7 October 2025
*/

#ifndef SIM_EPR_GENERATOR_h
#define SIM_EPR_GENERATOR_h

#include "sim/operable.h"
#include "sim/client.h"  // for QUBIT type

#include <unordered_set>
#include <vector>

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

    std::array<uint64_t, 8> s_occu_hist{};

    const size_t buffer_capacity_;
    const size_t max_cacheable_stores_;
private:
    size_t buffer_occu_{0};
    std::unordered_set<QUBIT> cached_qubits_;

    std::vector<MEMORY_MODULE*> memory_modules_;

    bool has_inflight_epr_generation_event_{false};
public:
    EPR_GENERATOR(double freq_khz, std::vector<MEMORY_MODULE*> memory_modules, size_t buffer_cap);

    void OP_init() override;

    void set_memory_modules(std::vector<MEMORY_MODULE*> memory_modules);

    void consume_epr_pairs(size_t count);
    void cache_qubit(QUBIT);
    void remove_qubit(QUBIT);
    void swap_qubit_for(QUBIT, QUBIT);

    void dump_deadlock_info();

    bool qubit_is_cached(QUBIT q) const { return cached_qubits_.find(q) != cached_qubits_.end(); }
    size_t get_occupancy() const        { return buffer_occu_; }
    bool has_capacity() const           { return buffer_occu_ + cached_qubits_.size() < buffer_capacity_; }

    bool store_is_cacheable() const { return cached_qubits_.size() < max_cacheable_stores_
                                                && has_capacity(); }

    const std::unordered_set<QUBIT>& get_cached_qubits() const { return cached_qubits_; }
protected:
    void OP_handle_event(event_type) override;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif  // SIM_EPR_GENERATOR_h
