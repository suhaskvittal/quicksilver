/*
 *  author: Suhas Vittal
 *  date:   12 January 2026
 * */

#ifndef SIM_MEMORY_SUBSYSTEM_h
#define SIM_MEMORY_SUBSYSTEM_h

#include "globals.h"
#include "sim/operable.h"

#include <iosfwd>
#include <unordered_set>
#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct MEMORY_ACCESS_RESULT
{
    bool success{false};

    /*
     * Time it takes to get the "result" of the request back.
     * Return value is in the cycles in the clock domain of servicing
     * memory tier.
     * */
    cycle_type latency;

    /*
     * Frequency of the memory level that gave the result.
     * */
    double freq_khz;
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
 * to return a `MEMORY_ACCESS_RESULT`. If the `success` field of this
 * result is set, then the storage is modified.
 *
 * Other required functions:
 *  (1) get_next_ready_cycle_for_load(QUBIT*): returns the earliest cycle
 *      where the load is feasible.
 * */

class MEMORY_LEVEL : public OPERABLE
{
public:
    using storage_type = std::unordered_set<QUBIT*>;

    /*
     * These are the [[n,k,d]] parameters of the underlying
     * storage (code block).
     * */
    const size_t storage_physical_qubit_count;
    const size_t storage_logical_qubit_count;
    const size_t storage_code_distance;

    /*
     * Total capacity of all storage here:
     * */
    const size_t num_blocks;
    const size_t total_capacity;

    virtual ~MEMORY_LEVEL() = default;
protected:
    std::vector<storage_type> blocks_;
public:
    MEMORY_LEVEL(std::string name, double freq_khz, size_t qubit_count, size_t n, size_t k, size_t d);

    /*
     * Stripes all qubits within the range across all code blocks in the system.
     * */
    template <class ITER>
    void striped_mapping(ITER begin, ITER end);

    /*
     * Memory access functions:
     * */
    MEMORY_ACCESS_RESULT do_load(QUBIT*);
    MEMORY_ACCESS_RESULT do_store(QUBIT*);
    MEMORY_ACCESS_RESULT do_coupled_load_store(QUBIT* ld, QUBIT* st);

    /*
     * Prints out information about the contents of this level to the given stream.
     * */
    void dump_storage_info(std::ostream&) const;

    /*
     * Estimates the next cycle when a load to the given qubit is possible.
     * */
    virtual cycle_type next_ready_cycle_for_load(QUBIT*) const =0;

protected:
    virtual MEMORY_ACCESS_RESULT load_impl(size_t idx, storage_type&, QUBIT*) =0;
    virtual MEMORY_ACCESS_RESULT store_impl(size_t idx, storage_type&, QUBIT*) =0;
    virtual MEMORY_ACCESS_RESULT coupled_load_store_impl(size_t idx, storage_type&, QUBIT* ld, QUBIT* st) =0;

    long operate() override { return 1; }
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Definition of MEMORY_LEVEL::striped_mapping()
 * */ 

template <class ITER> void 
MEMORY_LEVEL::striped_mapping(ITER begin, ITER end)
{
    assert(std::distance(begin, end) < total_capacity);

    size_t block_idx{0};
    std::for_each(begin, end,
            [this, &block_idx] (QUBIT* q)
            {
                this->blocks_[block_idx].insert(q);
                block_idx = (block_idx+1) % num_blocks;
            });
}


////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim

#endif // SIM_MEMORY_SUBSYSTEM_h
