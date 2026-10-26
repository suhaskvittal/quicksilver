/*
 *  author: Suhas Vittal
 *  date:   7 September 2026
 * */

#ifndef MEMORY_REMOTE_MEMORY_h
#define MEMORY_REMOTE_MEMORY_h

#include "sim/memory_level.h"
#include "sim/memory/bivariate_bicycle.h"
#include "sim/production.h"

#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Wraps a memory substrate so every access spends EPR credits from a shared
 * producer. Baseline memory uses ideal distribution; remote memory uses
 * entanglement distillation and also contends for the distiller's adapters.
 * */
template <class T>
class RemoteMemory : public T
{
public:
    using typename MemoryLevel::storage_type;

    /*
     * Statistics:
     * */
    uint64_t s_epr_consumed{0};
    uint64_t s_cycles_stalled_on_epr{0};
private:
    BulkProducer* epr_producer_;
    std::vector<cycle_type> epr_producer_adapter_ready_;
    // Production time of the resource batch selected for the current access.
    cycle_type access_resource_ready_cycle_{0};

    // This is a variable to speedup the pattern of `can_consume_epr() --> consume_epr()`
    mutable size_t epr_adapter_idx_{};
public:
    RemoteMemory(double freq_khz, size_t qubit_count, size_t n, size_t k, size_t d, BulkProducer* epr_producer);

    cycle_type next_ready_cycle_for_load(Qubit*) const override;
    cycle_type next_ready_cycle_for_access(Qubit*, Instruction::Type) const override;
    double     log_fidelity(Client*, double, double, double) const override;
    bool       has_epr_credit(Qubit*, Instruction::Type) const override;
protected:
    MemoryAccessResult load_impl(size_t idx, storage_type&, Qubit*) override;
    MemoryAccessResult store_impl(size_t idx, storage_type&, Qubit*) override;
    MemoryAccessResult coupled_load_store_impl(size_t idx, storage_type&, Qubit* ld, Qubit* st) override;
private:
    bool can_consume_epr(size_t amt) const;
    void consume_epr(size_t amt, cycle_type);
    cycle_type resource_ready_cycle(size_t amt) const;

    size_t epr_adapter_count() const;

    cycle_type intern_block_ready_cycle(size_t idx) const { return T::block_ready_cycle(idx); }
    cycle_type block_ready_cycle(size_t idx) const override;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#include "remote_memory.tpp"

#endif
