/*
 *  author: Suhas Vittal
 *  date:   12 January 2026
 * */

#ifndef SIM_STORAGE_h
#define SIM_STORAGE_h

#include "globals.h"

#include <unordered_set>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class Storage
{
public:
    using backing_buffer_type = std::unordered_set<Qubit*>;

    const size_t physical_qubit_count;
    const size_t logical_qubit_count;
    const size_t code_distance;
private:
    backing_buffer_type contents_;

};

class Storage : public Operable
{
public:
    using backing_buffer_type = std::unordered_set<Qubit*>;

    struct access_result_type
    {
        bool success{false};

        /*
         * Latency is given in terms of cycles for this `Storage`
         *     `critical_latency` is specifically any latency on the critical path.
         *     For example, for `coupled_load_store`, the store + data movement latency
         *     isn't on the critical path (only care about load outcome).
         * */
        cycle_type latency;
        cycle_type critical_latency;

        /*
         * We provide the frequency of the storage to help convert
         * to the desired cycles.
         * */
        double storage_freq_khz;
    };

    /*
     * These are just characteristics of the storage medium.
     * They have no simulator relevance (other than `logical_qubit_count`).
     * */
    const size_t physical_qubit_count;
    const size_t logical_qubit_count;
    const size_t code_distance;

    uint64_t s_surgery_operations{0};

    /*
     * Latency variables: relevant to simulation
     * */
    const cycle_type load_latency;
    const cycle_type store_latency;
private:
    backing_buffer_type contents_;

    /*
     * `cycle_available` is a vector that corresponds
     * to the cycle (of the `Storage`) when an adapter
     * becomes free. If the Storage supports random
     * access for all qubits, then there is one adapter
     * per qubit.
     * */
    std::vector<cycle_type> cycle_available_;
public:
    Storage(double freq_khz, 
            size_t n, size_t k, size_t d, 
            size_t num_adapters, 
            cycle_type load_latency, 
            cycle_type store_latency);
    
    bool contains(Qubit*) const;
    
    /*
     * Adds the given qubit to `contents_`. Should be used
     * only when initializing the storage.
     * */
    void insert(Qubit*);

    /*
     * Attempts a memory access to the given storage.
     * Fails if the storage cannot serve the memory access.
     *
     * Load -- qubit is removed from the memory
     * Store -- qubit is added to the memory
     * Coupled load store -- combination of both
     * */
    virtual access_result_type do_load(Qubit*);
    virtual access_result_type do_store(Qubit*);
    virtual access_result_type do_coupled_load_store(Qubit* ld, Qubit* st);

    /*
     * Returns true if any adapter is free this cycle.
     * */
    bool has_free_adapter() const;

    /*
     * Returns cycle when adapter becomes free.
     * */
    cycle_type next_free_adapter_cycle() const;

    /*
     * This function prints out information about the readiness of each adapter.
     * */
    void print_adapter_debug_info(std::ostream&) const;

    const backing_buffer_type& contents() const;
protected:
    enum class Access { LOAD, STORE, COUPLED_LOAD_STORE };

    long operate() override;

    /*
     * Common logic for all memory access functions (see above)
     * */
    virtual access_result_type do_memory_access(cycle_type access_latency, Access);
    
    /*
     * This function should contain the logic that implements the adapter manipulation.
     * Returns the latency of any adapter manipulations, or 0 if nothing was necessary.
     *
     * Note that the caller, `do_load` or `do_store`, will update the adapter ready time.
     * */
    virtual cycle_type adapter_access(std::vector<cycle_type>::iterator, Access);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Initializes the qubits for all clients by striping them
 * to maximize paralellism.
 *
 * Some pre-conditions:
 *  (1) It is assumed that the first storage in the vector is
 *      the compute subsystem's local memory. This memory is
 *      treated special and is split between the active clients.
 *  (2) The active clients correspond are the first in the
 *      `qubit_counts_by_client` array.
 * */

void storage_striped_initialization(const std::vector<Storage*>&,
                                    const std::vector<std::vector<Qubit*>>& qubits_by_client,
                                    size_t num_active_clients);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim

#endif  // SIM_STORAGE_h
