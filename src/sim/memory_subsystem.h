/*
 *  author: Suhas Vittal
 *  date:   12 January 2026
 * */

#ifndef SIM_MEMORY_SUBSYSTEM_h
#define SIM_MEMORY_SUBSYSTEM_h

#include "sim/storage.h"
#include "sim/routing_model.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class MEMORY_SUBSYSTEM
{
public:
    using access_result_type = STORAGE::access_result_type;
    using routing_base_type = ROUTING_MODEL<STORAGE>;
protected:
    std::vector<STORAGE*> storages_;
    std::unique_ptr<routing_base_type> routing_;
public:
    MEMORY_SUBSYSTEM(std::vector<STORAGE*>&&);

    /*
     * Finds the requisite `STORAGE*` location to serve
     * the access and executes `STORAGE::do_load`
     *
     * Note that the latencies in `access_result_type` are
     * converted for the caller within `do_memory_access`
     *
     * Same signature is used for `do_store`, but since a store doesn't
     * really care about the storage location, it will store the qubit
     * in the first available storage. Also, as the store is off the critical
     * path, the latency returned is 0 (but success/failure is still set).
     * */
    access_result_type do_load(QUBIT*, cycle_type caller_current_cycle, double caller_freq_khz);
    access_result_type do_store(QUBIT*, cycle_type caller_current_cycle, double caller_freq_khz);
    access_result_type do_coupled_load_store(QUBIT*, QUBIT*, cycle_type, double);

    /*
     * Searches for the `QUBIT*` that matches the given client id
     * and qubit id
     * */
    QUBIT* retrieve_qubit(client_id_type, qubit_type) const;

    /*
     * Estimates the next cycle when a load to the given qubit is possible.
     * */
    cycle_type get_next_ready_cycle_for_load(QUBIT*) const;

    const std::vector<STORAGE*>& storages() const;
private:
    access_result_type handle_access_outcome(access_result_type, 
                                             STORAGE*, 
                                             cycle_type c_current_cycle, 
                                             double c_freq_khz);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim

#endif // SIM_MEMORY_SUBSYSTEM_h
