/*
 *  author: Suhas Vittal
 *  date:   6 January 2026
 * */

#ifndef SIM_COMPUTE_SUBSYSTEM_h
#define SIM_COMPUTE_SUBSYSTEM_h

#include "sim/compute_base.h"

#include <array>
#include <deque>
#include <memory>
#include <unordered_set>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class COMPUTE_SUBSYSTEM : public COMPUTE_BASE
{
public:
    using inst_ptr = COMPUTE_BASE::inst_ptr;
    using ready_qubits_map = std::unordered_map<qubit_type, QUBIT*>;
    using ctx_switch_condition_type = std::pair<CLIENT*, CLIENT*>;

    /*
     * Information about a CLIENT's context:
     * */
    struct context_type
    {
        std::vector<QUBIT*> active_qubits;
        cycle_type          cycle_saved{};
    };

    const size_t   concurrent_clients;
    const size_t   total_clients;
    const uint64_t simulation_instructions;

    /*
     * Statistics:
     * */
    uint64_t s_context_switches{0};
private:
    std::vector<CLIENT*> all_clients_;

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
    std::deque<CLIENT*>  inactive_clients_;
    size_t               last_used_client_idx_{0};

    /*
     * Context switch information:
     *  `client_context_map_` contains information about a client's program state
     *  `context_switch_memory_access_buffer_` contains a list of memory accesses
     *      that must complete to execute a context switch. These have priority
     *      over everything else.
     * */
    std::vector<context_type>              client_context_table_;
    std::vector<std::pair<QUBIT*, QUBIT*>> context_switch_memory_access_buffer_;
public:
    COMPUTE_SUBSYSTEM(double                         freq_khz,
                        std::vector<std::string>     client_trace_files,
                        size_t                       local_memory_capacity,
                        size_t                       concurrent_clients,
                        uint64_t                     simulation_instructions,
                        std::vector<T_FACTORY_BASE*> top_level_t_factories,
                        MEMORY_SUBSYSTEM*            memory_hierarchy);
    ~COMPUTE_SUBSYSTEM();

    void print_progress(std::ostream&) const override;
    void print_deadlock_info(std::ostream&) const override;

    bool done() const;
    const std::vector<CLIENT*>& clients() const;
protected:
    long operate() override;
private:
    void handle_completed_clients();

    /*
     * During each `tick()`, the `OS` will check
     * if a context switch should occur using 
     * `context_switch_condition()`.
     * If the output is not {nullptr, *},
     * `do_context_switch()` is called.
     * */
    ctx_switch_condition_type context_switch_condition() const;
    void                      do_context_switch(CLIENT* incoming, CLIENT* outgoing);

    size_t fetch_and_execute_instructions_from_client(CLIENT*, const ready_qubits_map&);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim

#endif  // SIME_COMPUTE_SUBSYSTEM_h
