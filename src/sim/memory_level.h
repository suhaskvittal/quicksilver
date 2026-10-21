/*
 *  author: Suhas Vittal
 *  date:   12 January 2026
 * */

#ifndef SIM_MEMORY_SUBSYSTEM_h
#define SIM_MEMORY_SUBSYSTEM_h

#include "globals.h"
#include "instruction.h"
#include "sim/operable.h"
#include "sim/qubit.h"

#include <cassert>
#include <iosfwd>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct MemoryAccessResult
{
    enum class Code { SUCCESS, FAIL_UNAVAILABLE, FAIL_NO_CREDIT };

    Code code{Code::FAIL_UNAVAILABLE};

    /*
     * Time it takes to get the "result" of the request back.
     * Return value is in the cycles in the clock domain of servicing
     * memory tier.
     * */
    cycle_type latency{0};

    /*
     * Frequency of the memory level that gave the result.
     * */
    double freq_khz;

    bool success() const { return code == Code::SUCCESS; }
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Given that different types of codes operate differently,
 * we provide a generic abstract base class for implementing
 * a bunch of storage using the same error correction code.
 *
 * Subclasses must implement:
 *  (1) `load_impl()`
 *  (2) `store_impl()`
 *  (3) `coupled_load_store_impl()`
 * All three functions are given the index of the storage, a reference
 * to that storage, and the arguments. All three functions are expected
 * to return a `MemoryAccessResult`. If the `success` field of this
 * result is set, then the storage is modified.
 *
 * Other required functions:
 *  (1) get_next_ready_cycle_for_load(Qubit*): returns the earliest cycle
 *      where the load is feasible.
 * */

class MemoryLevel : public Operable
{
public:
    using storage_type = std::unordered_set<Qubit*>;

    const size_t storage_physical_qubit_count;
    const size_t storage_logical_qubit_count;
    const size_t storage_code_distance;

    /*
     * Total capacity of all storage here:
     * */
    const size_t num_blocks;
    const size_t total_capacity;

    /*
     * Statistics:
     * */
    uint64_t s_surgery_operations{0};
    uint64_t s_shift_automorphisms{0};
protected:
    std::vector<storage_type> blocks_;

    /*
     * Cache of each resident qubit's block index, populated on store and refreshed
     * opportunistically by `block_index_of()` whenever it has to fall back to a full
     * scan. `mutable` because that fallback path is reached from a `const` caller.
     * */
    mutable std::unordered_map<Qubit*, size_t> qubit_to_block_idx_;
public:
    MemoryLevel(std::string name, double freq_khz, size_t qubit_count, size_t n, size_t k, size_t d);
    virtual ~MemoryLevel() = default;

    /*
     * Stripes all qubits within the range across all code blocks in the system.
     * */
    template <class Iter>
    void striped_mapping(Iter begin, Iter end);

    /*
     * Memory access functions:
     * */
    MemoryAccessResult do_load(Qubit*);
    MemoryAccessResult do_store(Qubit*);
    MemoryAccessResult do_coupled_load_store(Qubit* ld, Qubit* st);

    /*
     * Prints out information about the contents of this level to the given stream.
     * */
    void dump_storage_info(std::ostream&) const;

    /*
     * Estimates the next cycle when a load to the given qubit is possible.
     * */
    virtual cycle_type next_ready_cycle_for_load(Qubit*) const =0;

    /*
     * Returns fidelity of memory subsystem for client's application.
     * `scale` indicates the amount to scale values such as cycles or
     * number of operations by. `d_freq_khz` is the frequency of the driver.
     * */
    virtual double log_fidelity(Client*, double scale, double d_freq_khz, double phys_error) const =0;

    /*
     * True if an access of the given type touching `q` currently has the EPR credit it
     * would need to proceed (2 credits for `COUPLED_LOAD_STORE`, 1 otherwise). Trivially
     * true for the bare substrate; `RemoteMemory` checks the shared credit pool
     * for both ideal baseline distribution and remote distillation.
     * */
    virtual bool has_epr_credit(Qubit*, Instruction::Type) const { return true; }

    const std::vector<storage_type>& blocks() const { return blocks_; }
protected:
    /*
     * Returns the index of the block holding `q`. If `q` is not resident in this
     * level, returns 0.
     * */
    size_t block_index_of(Qubit*) const;

    virtual MemoryAccessResult load_impl(size_t idx, storage_type&, Qubit*) =0;
    virtual MemoryAccessResult store_impl(size_t idx, storage_type&, Qubit*) =0;
    virtual MemoryAccessResult coupled_load_store_impl(size_t idx, storage_type&, Qubit* ld, Qubit* st) =0;

    long operate() override { return 1; }
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Definition of MemoryLevel::striped_mapping()
 * */ 

template <class Iter> void 
MemoryLevel::striped_mapping(Iter begin, Iter end)
{
    assert(std::distance(begin, end) < total_capacity);

    size_t block_idx{0};
    std::for_each(begin, end,
            [this, &block_idx] (Qubit* q)
            {
                this->blocks_[block_idx].insert(q);
                this->qubit_to_block_idx_[q] = block_idx;
                block_idx = (block_idx+1) % num_blocks;
            });
}


////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim

#endif // SIM_MEMORY_SUBSYSTEM_h
