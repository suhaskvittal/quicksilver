/*
 *  author: Suhas Vittal
 *  date:   6 January 2026
 * */

#ifndef SIM_COMPUTE_SUBSYSTEM_h
#define SIM_COMPUTE_SUBSYSTEM_h

#include "sim/client.h"
#include "sim/factory.h"
#include "sim/storage.h"

#include <array>
#include <memory>
#include <unordered_set>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class COMPUTE_SUBSYSTEM : public OPERABLE
{
public:
    using inst_ptr = CLIENT::inst_ptr;
    using ready_qubits_map = std::unordered_map<qubit_type, QUBIT*>;

    /*
     * Information about a CLIENT's context:
     * */
    struct context_type
    {
        std::unordered_set<QUBIT*> active_set;
        cycle_type                 cycle_saved;
    };

    const size_t qubit_capacity;
    const size_t concurrent_clients;

    /*
     * Statistics:
     * */
    uint64_t s_context_switches{0};
private:
    std::unique_ptr<STORAGE> local_memory_;

    /*
     * Only a subset of clients can execute on the
     * device due to limited capacity. Clients can
     * be moved in an out of the active set via
     * a `context_switch` (see below)
     *
     * `last_used_client_idx_` is used to ensure
     * fairness when executing instructions.
     * */
    std::vector<CLIENT*> active_clients_;
    size_t               last_used_client_idx_{0};

    /*
     * Context switch information:
     * */
    std::unordered_map<CLIENT*, context_type> client_context_map_;

    /*
     * Pointer to memory subsystem
     * */
    MEMORY_SUBSYSTEM* memory_hierarchy_;

    /*
     * `top_level_t_factories_` are used to perform T gates
     * */
    std::vector<T_FACTORY_BASE*> top_level_t_factories_;
public:
    void print_progress(std::ostream&) const override;
    void print_deadlock_info(std::ostream&) const override;

    void context_switch(CLIENT* incoming, CLIENT* outgoing);

    const std::unique_ptr<STORAGE>& local_memory() const;
protected:
    long operate() override;
private:
    size_t fetch_and_execute_instructions_from_client(CLIENT*, const ready_qubits_map&);

    /*
     * Tries to execute a given instruction. `args` corresponds to
     * the `QUBIT*`s that map to each of the instruction's arguments
     *
     * Returns true on a successful execution.
     * */
    bool execute_instruction(inst_ptr, std::array<QUBIT*, 3>&& args);

    /*
     * Helper functions for `execute_instruction`.
     * */
    bool do_h_or_s_gate(inst_ptr, QUBIT*);
    bool do_cx_like_gate(inst_ptr, QUBIT* ctrl, QUBIT* target);
    bool do_t_like_gate(inst_ptr, QUBIT*);
    bool do_memory_access(inst_ptr, QUBIT* ld, QUBIT* st);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim

#endif  // SIME_COMPUTE_SUBSYSTEM_h
