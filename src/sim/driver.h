/*
 *  author: Suhas Vittal
 *  date:   12 March 2026
 * */

#ifndef SIM_DRIVER_h
#define SIM_DRIVER_h

#include "globals.h"
#include "sim/client.h"
#include "sim/compute_subsystem.h"
#include "sim/driver/rdr.h"
#include "sim/memory_level.h"
#include "sim/production.h"
#include "sim/stall_monitor.h"

#include <array>
#include <utility>
#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class DRIVER : public OPERABLE
{
public:
    using inst_ptr = CLIENT::inst_ptr;

    /*
     * Information about a CLIENT's context:
     * */
    struct context_type
    {
        std::vector<QUBIT*> active_qubits;
        cycle_type          cycle_saved{};
    };

    enum class RDR_LOOKUP_RESULT { RETIRE, NEEDS_CORRECTION, IN_PROGRESS, NOT_FOUND };

    /*
     * Stall monitor:
     *  `STALL_TYPE` contains the stalls tracked by the compute subsystem. Feel free
     *      to add to this type if you want new stats.
     *  `STALL_MONITOR` tracks where these stalls occur.
     * */
    enum class STALL_TYPE { MEMORY, MAGIC_STATE, RPC, EPR, SIZE };
    using stall_monitor_type = STALL_MONITOR<static_cast<size_t>(STALL_TYPE::SIZE), STALL_TYPE>;

    uint64_t cycles_without_progress{0};

    const size_t   concurrent_clients;
    const size_t   total_clients;
    const uint64_t simulation_instructions;

    /*
     * Statistics:
     * */
    uint64_t s_context_switches{0};
    uint64_t s_rotation_instructions{0};
private:
    std::vector<CLIENT*> clients_;

    /*
     * Only a subset of clients can execute on the
     * device due to limited capacity. Clients can
     * be moved in and out of the compute subsystem
     * via context switches.
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

    COMPUTE_SUBSYSTEM* compute_subsystem_;
    std::vector<PRODUCER_BASE*> t_factories_;
    std::vector<MEMORY_LEVEL*> memory_subsystem_;

    /*
     * Rotation directed runahead logic (RDR):
     * */
    driver::ROTATION_DIRECTED_RUNAHEAD* rdr_{nullptr};

    /*
     * `stall_monitor_` manages statistics related to stalls
     * */
    stall_monitor_type stall_monitor_;
public:
    DRIVER(std::vector<std::string> client_trace_files, 
            size_t concurrent_clients,
            uint64_t simulation_instructions,
            COMPUTE_SUBSYSTEM*,
            std::vector<PRODUCER_BASE*> top_level_t_factories,
            std::vector<MEMORY_LEVEL*>);
    ~DRIVER();

    void print_progress(std::ostream&) const override;
    void print_deadlock_info(std::ostream&) const override;

    bool done() const;

    /*
     * This function should be called at the end of the simulation to cleanup any
     * stats.
     * */
    void stop_simulation();

    COMPUTE_SUBSYSTEM* compute_subsystem() const;
    const std::vector<CLIENT*>& clients() const;
    const stall_monitor_type& stall_monitor() const;
    driver::ROTATION_DIRECTED_RUNAHEAD* rdr() const;
protected:
    long operate() override;
private:
    void handle_completed_clients();

    /*
     * During each `operate()`, the driver will check
     * if a context switch should occur using 
     * `context_switch_condition()`.
     * If the output is not {nullptr, *},
     * `do_context_switch()` is called.
     * */
    std::pair<CLIENT*, CLIENT*> context_switch_condition() const;
    void                        do_context_switch(CLIENT* incoming, CLIENT* outgoing);

    /*
     * This is a wrapper for `CLIENT::retire_instruction` that
     * handles stats before retiring the instruction.
     * */
    void retire_instruction(CLIENT*, inst_ptr, cycle_type instruction_latency);

    long fetch_and_execute_instructions_from_client(CLIENT*);

    /*
     * Updates instruction stats when it is iterated through the `front_layer` in 
     * `fetch_and_execute_instructions_from_client`. This will occur once all of
     * its dependencies are retired, regardless of the ready status of its operands.
     *
     * Note that this function is called every time it appears, which can occur 
     * if the instruction could not execute the first time it appeared. Ensure
     * that any logic does not assume that this is the first call for the
     * input instruction.
     * */
    void update_instruction_stats_on_fetch(inst_ptr, const std::vector<QUBIT*>& operands);

    /*
     * This function is only called once per uop (or once total if the instruction
     * has no uops).
     *
     * Note that `retire_instruction` is used to commit stats, so stat updates should
     * happen here.
     * */
    void update_instruction_stats_before_retire(inst_ptr);

    /*
     * Returns true if the instruction, with the given operands, is ready for execution
     * in the current cycle.
     * */
    bool is_instruction_ready(inst_ptr, const std::vector<QUBIT*>& operands) const;

    /*
     * RDR implementation -------------------------------------------------------------
     * */

    bool              rdr_handle_instruction(CLIENT*, inst_ptr, QUBIT*);
    RDR_LOOKUP_RESULT rdr_lookup_instruction(inst_ptr, QUBIT*);
    void              rdr_do_runahead(CLIENT*, inst_ptr);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#endif // SIM_DRIVER_h
