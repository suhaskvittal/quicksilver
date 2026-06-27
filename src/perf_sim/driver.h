/*
 *  author: Suhas Vittal
 *  date:   12 March 2026
 * */

#ifndef SIM_DRIVER_h
#define SIM_DRIVER_h

#include "globals.h"
#include "perf_sim/client.h"
#include "perf_sim/compute_subsystem.h"
#include "perf_sim/driver/rdr.h"
#include "perf_sim/memory_level.h"
#include "perf_sim/production.h"
#include "perf_sim/stall_monitor.h"

#include <array>
#include <utility>
#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class Driver : public Operable
{
public:
    using inst_ptr = Client::inst_ptr;

    /*
     * Information about a Client's context:
     * */
    struct context_type
    {
        std::vector<Qubit*> active_qubits;
        cycle_type          cycle_saved{};
    };

    /*
     * Stall monitor:
     *  `Stall` contains the stalls tracked by the compute subsystem. Feel free
     *      to add to this type if you want new stats.
     *  `StallMonitor` tracks where these stalls occur.
     * */
    enum class Stall { MEMORY, MAGIC_STATE, RPC, EPR, SIZE };
    using stall_monitor_type = StallMonitor<static_cast<size_t>(Stall::SIZE), Stall>;

    /*
     * Fidelity output type
     * */
    struct fidelity_data_type
    {
        // total application fidelity
        double overall{};

        // breakdown of fidelity by component:
        double compute{};
        double mem{};
        double rdr{};
    };

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
    std::vector<Client*> clients_;

    /*
     * Only a subset of clients can execute on the
     * device due to limited capacity. Clients can
     * be moved in and out of the compute subsystem
     * via context switches.
     *
     * `last_used_client_idx_` is used to ensure
     * fairness when executing instructions.
     * */
    std::vector<Client*> active_clients_;
    std::deque<Client*>  inactive_clients_;
    size_t               last_used_client_idx_{0};

    /*
     * Context switch information:
     *  `client_context_map_` contains information about a client's program state
     *  `context_switch_memory_access_buffer_` contains a list of memory accesses
     *      that must complete to execute a context switch. These have priority
     *      over everything else.
     * */
    std::vector<context_type>              client_context_table_;
    std::vector<std::pair<Qubit*, Qubit*>> context_switch_memory_access_buffer_;

    ComputeSubsystem* compute_subsystem_;
    std::vector<ProducerBase*> t_factories_;
    std::vector<MemoryLevel*> memory_subsystem_;

    /*
     * Rotation directed runahead logic (RDR):
     * */
    driver::RotationDirectedRunahead* rdr_{nullptr};

    /*
     * `stall_monitor_` manages statistics related to stalls
     * */
    stall_monitor_type stall_monitor_;
public:
    Driver(std::vector<std::string> client_trace_files, 
            size_t concurrent_clients,
            uint64_t simulation_instructions,
            ComputeSubsystem*,
            std::vector<ProducerBase*> top_level_t_factories,
            std::vector<MemoryLevel*>);
    ~Driver();

    void print_progress(std::ostream&) const override;
    void print_deadlock_info(std::ostream&) const override;

    bool done() const;

    /*
     * This function should be called at the end of the simulation to cleanup any
     * stats.
     * */
    void stop_simulation();

    /*
     * Estimates application fidelity for given client. As the simulator does not
     * typically evaluate the entire application, the user should supply the total
     * number of instructions in the program so we can scale results accordingly.
     * */
    fidelity_data_type application_fidelity(int client_id, uint64_t scale_to_inst, double p) const;

    ComputeSubsystem* compute_subsystem() const;
    const std::vector<Client*>& clients() const;
    const stall_monitor_type& stall_monitor() const;
    driver::RotationDirectedRunahead* rdr() const;
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
    std::pair<Client*, Client*> context_switch_condition() const;
    void                        do_context_switch(Client* incoming, Client* outgoing);

    /*
     * This is a wrapper for `Client::retire_instruction` that
     * handles stats before retiring the instruction.
     * */
    void retire_instruction(Client*, inst_ptr, cycle_type instruction_latency);

    long fetch_and_execute_instructions_from_client(Client*);

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
    void update_instruction_stats_on_fetch(inst_ptr, const std::vector<Qubit*>& operands);

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
    bool is_instruction_ready(inst_ptr, const std::vector<Qubit*>& operands) const;

    /*
     * RDR implementation -------------------------------------------------------------
     * */

    enum class RDRLookupResult { RETIRE, NEEDS_CORRECTION, IN_PROGRESS, NOT_FOUND };

    bool rdr_handle_instruction(Client*, inst_ptr, Qubit*);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#endif // SIM_DRIVER_h
