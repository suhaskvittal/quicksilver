/*
 *  author: Suhas Vittal
 *  date:   12 January 2026
 * */

#ifndef SIM_MEMORY_SUBSYSTEM_h
#define SIM_MEMORY_SUBSYSTEM_h

#include "globals.h"

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
 * we provide a generic class for implementing a bunch of storage
 * using the same error correction code.
 *
 * This class is a CRTP. `IMPL` must implement the following:
 *  (1) `load_impl()` 
 *  (2) `store_impl()`
 *  (3) `coupled_load_store_impl()`
 * All three functions are given the index of the storage, a reference
 * to that storage, and the arguments. All three functions are expected
 * to return a `MEMORY_ACCESS_RESULT`. If the `success` field of this
 * result is set, then the storage is modified.
 *
 * Other good to have functions:
 *  (1) get_next_ready_cycle_for_load_impl(QUBIT*): returns the earliest cycle
 *      where the load is feasible. Called by `get_next_ready_cycle_for_load()`
 * */

template <class IMPL>
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
    const size_t total_capacity;
protected:
    std::vector<storage_type> blocks_;
public:
    MEMORY_LEVEL(std::string_view name, double freq_khz, size_t qubit_count, size_t n, size_t k, size_t d);

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
    cycle_type get_next_ready_cycle_for_load(QUBIT*) const;
protected:
    long operate() override {}
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim

#include "memory_level.tpp"

#endif // SIM_MEMORY_SUBSYSTEM_h
