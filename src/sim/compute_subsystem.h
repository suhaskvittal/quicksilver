/*
 *  author: Suhas Vittal
 *  date:   6 January 2026
 * */

#ifndef SIM_COMPUTE_SUBSYSTEM_h
#define SIM_COMPUTE_SUBSYSTEM_h

#include "sim/compute_base.h"
#include "sim/rotation_subsystem.h"

#include <array>
#include <deque>
#include <memory>
#include <unordered_set>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct compute_extended_config
{
    /* RPC parameters */
    bool   rpc_enabled{false};
    double rpc_freq_khz;
    int64_t rpc_capacity{2};
    double rpc_watermark{0.5};
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class COMPUTE_SUBSYSTEM : public COMPUTE_BASE
{
public:
    using inst_ptr = COMPUTE_BASE::inst_ptr;
    using execute_result_type = COMPUTE_BASE::execute_result_type;
    using ctx_switch_condition_type = std::pair<CLIENT*, CLIENT*>;

    /*
     * Information about a CLIENT's context:
     * */
    struct context_type
    {
        std::vector<QUBIT*> active_qubits;
        cycle_type          cycle_saved{};
    };

    enum class RPC_LOOKUP_RESULT { NOT_FOUND, IN_PROGRESS, NEEDS_CORRECTION, RETIRE };

    const size_t   concurrent_clients;
    const size_t   total_clients;
    const uint64_t simulation_instructions;

    uint64_t cycles_without_progress{0};

    /*
     * Statistics:
     * */
    uint64_t s_magic_state_produced_sum{0}; // note that this is used to compute T bandwidth
    uint64_t s_context_switches{0};

    uint64_t s_total_rotations{0};
    uint64_t s_successful_rpc{0};
    uint64_t s_total_rpc{0};
    uint64_t s_cycles_with_rpc_stalls{0};
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

    /*
     * For calculating `s_magic_state_avail_sum` -- this is the number of magic states
     * available last cycle
     * */
    size_t magic_states_avail_last_cycle_{0};

    /*
     * Subsystem for pre-computed rotation gates. If `nullptr`, then it is disabled.
     * */
    ROTATION_SUBSYSTEM* rotation_subsystem_{nullptr};

    /*
     * `had_rpc_stall_this_cycle_` is used to update `s_cycles_with_rpc_stalls`
     * */
    bool had_rpc_stall_this_cycle_;
public:
    COMPUTE_SUBSYSTEM(double                         freq_khz,
                        std::vector<std::string>     client_trace_files,
                        size_t                       code_distance,
                        size_t                       local_memory_capacity,
                        size_t                       concurrent_clients,
                        uint64_t                     simulation_instructions,
                        std::vector<PRODUCER_BASE*>  top_level_t_factories,
                        MEMORY_SUBSYSTEM*            memory_hierarchy,
                        compute_extended_config);
    ~COMPUTE_SUBSYSTEM();

    void print_progress(std::ostream&) const override;
    void print_deadlock_info(std::ostream&) const override;

    bool done() const;
    const std::vector<CLIENT*>& clients() const;

    ROTATION_SUBSYSTEM* rotation_subsystem() const;

    /*
     * rpc = "Rotation Pre-Computation". This returns true if `rotation_subsystem_ != nullptr`
     * */
    bool is_rpc_enabled() const;

    /*
     * In long simulations with long stall periods, it is sometimes useful to "skip"
     * several cycles where nothing is happening.
     *
     * Conditions (in the current simulator) are simple:
     *  (1) all instructions at the head of the DAG are memory instructions (for all clients)
     *  (2) all T factories have full buffers
     *  (3) rotation subsystem has no active operations
     *
     * If all three criteria are met, then this function returns the cycle where
     * the earliest storage becomes available.
     *
     * Currently, assumes all very long latency stalls are memory stalls.
     * */
    std::optional<cycle_type> skip_to_cycle() const;
protected:
    long operate() override;
private:
    void handle_completed_clients();

    /*
     * This is a wrapper for `CLIENT::retire_instruction` that
     * handles stats before retiring the instruction.
     * */
    void retire_instruction(CLIENT*, inst_ptr, cycle_type instruction_latency);

    /*
     * During each `tick()`, the `OS` will check
     * if a context switch should occur using 
     * `context_switch_condition()`.
     * If the output is not {nullptr, *},
     * `do_context_switch()` is called.
     * */
    ctx_switch_condition_type context_switch_condition() const;
    void                      do_context_switch(CLIENT* incoming, CLIENT* outgoing);

    long fetch_and_execute_instructions_from_client(CLIENT*);

    /*
     * Performs all rpc operations on an instruction (logical flow is handled within function).
     * Returns true if instruction should be skipped in the iteration of 
     * `fetch_and_execute_instructions_from_client()`
     * */
    bool rpc_handle_instruction(CLIENT*, inst_ptr, QUBIT*);

    /*
     * On the first pass to a rotation gate, this function does:
     *  (1) If the rotation is already precomputed, then the gate is teleported onto the
     *      given qubit.
     *          Upon success, this function returns `RPC_LOOKUP_RESULT::RETIRE`.
     *          Upon failure, this function returns `RPC_LOOKUP_RESULT::NEEDS_CORRECTION`
     *          to indicate that the corrective gate must be applied.
     *
     *  (2) If the rotation is partially recomputed, then `RPC_LOOKUP_RESULT::IN_PROGRESS`
     *      is returned. The compute subsystem can either choose to invalidate the rotation or
     *      wait until the rotation gate completes.
     *
     *  (3) If the rotation is not present, then `RPC_LOOKUP_RESULT::NOT_FOUND` is returned
     *      and the rotation must execute as normal.
     * */
    RPC_LOOKUP_RESULT rpc_lookup_rotation(inst_ptr, QUBIT*);

    /*
     * This function finds a rotation dependent on the given instruction
     * deeper into the given client's DAG.
     * If a rotation is found, then the function will try to allocate
     * a qubit for it in `rotation_subsystem_`.
     * */
    void rpc_find_and_attempt_allocate_for_future_rotation(CLIENT*, inst_ptr);

    /*
     * Computes the first cycle when the instruction can be executed. If the
     * instruction cannot be executed, this function returns std::nullopt.
     * */
    std::optional<cycle_type> get_next_ready_cycle_for_instruction(CLIENT*, inst_ptr) const;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim

#endif  // SIM_COMPUTE_SUBSYSTEM_h
