/*
 *  author: Suhas Vittal
 *  date:   11 March 2026
 * */

#ifndef SIM_MEMORY_QLDPC_MEMORY_h
#define SIM_MEMORY_QLDPC_MEMORY_h

#include "sim/memory_level.h"
#include "sim/production.h"

#include <algorithm>
#include <limits>
#include <optional>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class QLDPCMemory : public MemoryLevel
{
public:
    using typename MemoryLevel::storage_type;

    struct config_type
    {
        size_t extractors_per_block{2};
        cycle_type surgery_latency{std::numeric_limits<cycle_type>::max()},  // default: storage_code_distance
                   shift_aut_latency{2};
    };

    struct block_data_type
    {
        /*
         * for simplicity, just assume that the extractors are always connected to
         * some adapter... so we only need to care about extractor availability.
         * */
        std::vector<cycle_type> extractors;

        block_data_type(size_t num_ext);

        auto ready_extractor(cycle_type c) -> std::vector<cycle_type>::iterator;
        auto ready_extractor(cycle_type c) const -> std::vector<cycle_type>::const_iterator;
        bool is_available(cycle_type c) const { return ready_extractor(c) != extractors.end(); }
        cycle_type next_ready_cycle() const { return *std::min_element(extractors.begin(), extractors.end()); }
    };

    const config_type conf;
    uint64_t s_epr_consumed{0};
protected:
    std::vector<block_data_type> prog_block_data_;

    /*
     * Support for remote memory and entanglement distillation
     *
     * While the EPR storage is not implemented as a monolithic entity, we will assume
     * that the system is able to effectively load balance so that there is not a situation
     * where extractors are available and EPR qubits are present, but we cannot do anything
     * since the EPR qubits are in an unavailable block.
     * */
    block_data_type epr_block_data_;
    BulkProducer* epr_producer_{nullptr};
public:
    QLDPCMemory(double freq_khz, size_t qubit_count, size_t n, size_t k, size_t d);
    QLDPCMemory(double freq_khz, size_t qubit_count, size_t n, size_t k, size_t d,
                BulkProducer* epr_producer);
    QLDPCMemory(double freq_khz,
                size_t qubit_count,
                size_t n,
                size_t k,
                size_t d,
                config_type,
                BulkProducer* epr_producer=nullptr);

    cycle_type next_ready_cycle_for_load(Qubit*) const override;
    cycle_type next_ready_cycle_for_access(Qubit*, Instruction::Type) const override;
    bool has_epr_credit(Qubit*, Instruction::Type) const override;
    double log_fidelity(Client*, double, double, double) const override;

    cycle_type surgery_latency() const { return (conf.surgery_latency == std::numeric_limits<cycle_type>::max()) 
                                                ? storage_code_distance : conf.surgery_latency; }
protected:
    MemoryAccessResult load_impl(size_t idx, storage_type&, Qubit*) override;
    MemoryAccessResult store_impl(size_t idx, storage_type&, Qubit*) override;
    MemoryAccessResult coupled_load_store_impl(size_t idx, storage_type&, Qubit* ld, Qubit* st) override;
private:
    /*
     * Adjusts the latency of a surgery operation to account for shift automorphisms.
     * The surgery operation will begin at cycle `st` and the extractor/ancilla system
     * must be ready at cycle `r`
     * */
    cycle_type adjust_surgery_latency(cycle_type st, cycle_type r) const;

    /*
     * Attempts to do surgery operation on block, consuming EPR qubit if needed.
     * `num_ops` is the number of operations to do (assumed that this group of operations
     * consumes, in total, one EPR qubit).
     *      -- for example, for a load, `num_ops == 2`
     *      -- for a store, `num_ops == 1`
     *      -- a coupled load store calls this function two separate times
     * */
    std::optional<cycle_type> do_inter_block_surgery(size_t idx,
                                                    cycle_type start_cycle,
                                                    size_t num_ops,
                                                    bool shift_aut_needed=true);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#endif // SIM_MEMORY_QLDPC_MEMORY_h
