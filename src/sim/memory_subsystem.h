/*
 *  author: Suhas Vittal
 *  date:   12 January 2026
 * */

#ifndef SIM_MEMORY_SUBSYSTEM_h
#define SIM_MEMORY_SUBSYSTEM_h

#include "sim/storage.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class MEMORY_SUBSYSTEM
{
public:
    using access_result_type = STORAGE::access_result_type;
private:
    std::vector<STORAGE*> storages_;
public:
    MEMORY_SUBSYSTEM(std::vector<STORAGE*>&&);

    /*
     * `tick` in this context calls `STORAGE::tick()` for
     * all `STORAGE*` in `storages_`
     * */
    void tick();

    /*
     * Finds the requisite `STORAGE*` location to serve
     * the access and executes `STORAGE::do_memory_access`
     * */
    access_result_type do_memory_access(QUBIT* ld, QUBIT* st);

    /*
     * Searches for the `QUBIT*` that matches the given client id
     * and qubit id
     * */
    QUBIT* retrieve_qubit(client_id_type, qubit_type) const;

    const std::vector<STORAGE*>& storages() const;
private:
    /*
     * Returns an iterator to the `STORAGE*` containing
     * the given qubit.
     * */
    std::vector<STORAGE*>::iterator       lookup(QUBIT*);
    std::vector<STORAGE*>::const_iterator lookup(QUBIT*) const;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim

#endif // SIM_MEMORY_SUBSYSTEM_h
