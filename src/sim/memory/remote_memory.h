/*
 *  author: Suhas Vittal
 *  date:   7 September 2026
 * */

#ifndef MEMORY_REMOTE_MEMORY_h
#define MEMORY_REMOTE_MEMORY_h

#include "sim/memory_level.h"
#include "sim/memory/bivariate_bicycle.h"
#include "sim/production.h"

#include <type_traits>
#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Wraps a `MemoryLevel` implementation so that it is operated as a *remote* region:
 * the memory substrate is only reachable over entanglement, so every access must
 * spend EPR credits held locally by the code block being accessed. Credits are
 * replenished from `epr_producer_`, which distills them.
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
public:
    RemoteMemory(double freq_khz, size_t qubit_count, size_t n, size_t k, size_t d, BulkProducer* epr_producer);

    cycle_type next_ready_cycle_for_load(Qubit*) const override;
    double     log_fidelity(Client*, double, double, double) const override;
    bool       has_epr_credit(Qubit*, Instruction::Type) const override;
protected:
    MemoryAccessResult load_impl(size_t idx, storage_type&, Qubit*) override;
    MemoryAccessResult store_impl(size_t idx, storage_type&, Qubit*) override;
    MemoryAccessResult coupled_load_store_impl(size_t idx, storage_type&, Qubit* ld, Qubit* st) override;
private:
    bool can_consume_epr(size_t amt) const;
    void consume_epr(size_t amt, cycle_type);

    size_t epr_adapter_count() const;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#include "remote_memory.tpp"

#endif
