/*
 *  author: Suhas Vittal
 *  date:   11 March 2026
 * */

#ifndef SIM_MEMORY_BIVARIATE_BICYCLE_h
#define SIM_MEMORY_BIVARIATE_BICYCLE_h

#include "sim/memory_level.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class BBMemory : public MemoryLevel
{
public:
    using typename MemoryLevel::storage_type;

protected:
    /*
     * `adapters_` contains the ready cycle for each BB code adapter.
     * */
    std::vector<cycle_type> adapters_;

    /* a shift automorphism costs at most this many cycles */
    constexpr static cycle_type SHIFT_AUT_LATENCY{2};

public:
    // Teleportation substrate; RemoteMemory supplies EPR credits for every access.
    BBMemory(double freq_khz, size_t qubit_count, size_t n, size_t k, size_t d);

    cycle_type next_ready_cycle_for_load(Qubit*) const override;
    double log_fidelity(Client*, double, double, double) const override;
protected:
    /*
     * These are `protected` (not `private`) so that wrappers such as
     * `RemoteMemory<BBMemory>` can delegate to them.
     * */
    MemoryAccessResult load_impl(size_t idx, storage_type&, Qubit*) override;
    MemoryAccessResult store_impl(size_t idx, storage_type&, Qubit*) override;
    MemoryAccessResult coupled_load_store_impl(size_t idx, storage_type&, Qubit* ld, Qubit* st) override;

private:
    /*
     * Latency of a `d`-round surgery operation that begins at cycle `ref`, including the shift
     * automorphism if the adapter has not been idle long enough by then to hide it.
     * */
    cycle_type latency_of_surgery_operation(cycle_type ref, size_t idx) const;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#endif // SIM_MEMORY_BIVARIATE_BICYCLE_h
