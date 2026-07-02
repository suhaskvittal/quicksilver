/*
 *  author: Suhas Vittal
 *  date:   11 March 2026
 * */

#ifndef SIM_COMPUTE_SUBSYSTEM_h
#define SIM_COMPUTE_SUBSYSTEM_h

#include "globals.h"
#include "instruction.h"
#include "sim/production.h"
#include "sim/memory_level.h"
#include "sim/routing/multi_channel_bus.h"

#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class ComputeSubsystem : public Operable
{
public:
    using inst_ptr = Instruction*;
    using local_storage_type = std::vector<Qubit*>;
    using production_level_type = std::vector<ProducerBase*>;
    using memory_subsystem_type = std::vector<MemoryLevel*>;
    
    /*
     * Result of executing a quantum gate:
     *  `progress` indicates if the gate was successful,
     *  `latency` indicates how long the gate will take to finish.
     * */
    struct execute_result_type
    {
        long       progress{0};
        cycle_type latency;
    };

    /*
     * Routing implementation:
     * */
    struct routing_type : routing::MultiChannelBus<routing_type>
    {
        using routing::MultiChannelBus<routing_type>::id_type;

        ComputeSubsystem* c;

        routing_type(ComputeSubsystem*);
        id_type translate(Qubit*) const;
    };

    /*
     * Array type for tracking instruction frequency
     * */
    using inst_usage_array = std::array<uint64_t, static_cast<size_t>(Instruction::Type::NIL)>;

    const size_t code_distance;
    const size_t local_memory_capacity;

    /*
     * Dedicated ancilla are any ancilla that are used for some policy,
     * such as ancilla space dedicated for auto-correction.
     *
     * This is mainly used so that we know where to route.
     * */
    const size_t dedicated_ancilla_count;

    /*
     * Statistics:
     * */

    inst_usage_array s_inst_executed_by_type{};
private:
    local_storage_type local_memory_;
    production_level_type t_factories_;
    memory_subsystem_type memory_subsystem_;

    routing_type routing_;

    local_storage_type dedicated_ancilla_;

    /*
     * `memory_level_map_` is used to accelerate lookups into the
     * `memory_subsystem_`. If a qubit has a idx of -1, then it
     * indexes into `local_memory_`.
     * */
    std::unordered_map<Qubit*, ssize_t> memory_level_map_;

    /*
     * Prevent multiple calls to RLTP from happening too soon.
     * */
    cycle_type rltp_ready_cycle_{0};
public:
    ComputeSubsystem(double freq_khz, 
                        size_t code_distance, 
                        size_t local_memory_capacity,
                        production_level_type t_factories,
                        memory_subsystem_type memory_subsystem);
    ~ComputeSubsystem();

    void initialize_qubits(std::vector<Qubit*> program_qubits);

    /*
     * Instruction execution: `inst_ptr` is the instruction to be executed,
     * and the second operand are the pointers to the qubits this instruction
     * operates on.
     * */
    execute_result_type execute_instruction(inst_ptr, std::vector<Qubit*>);

    /*
     * Implementation of a rotation gate's uops using reaction-limited
     * T teleportation. This is tailored to rotation gates, and thus this
     * function will consume an arbitrary number of uops (given resources
     * are available).
     * */
    execute_result_type do_rotation_via_rltp(inst_ptr, Qubit*, size_t limit);

    /*
     * Returns true if the qubit is in `local_memory_`
     * */
    bool is_qubit_in_local_memory(const Qubit*) const;

    /*
     * These are functions for simulating operations with rotation-directed
     * runahead. They should only be called by `RotationDirectedRunahead`.
     *
     * These functions only model the routing overheads of the given operations.
     * The caller must update the cycle availbility.
     *
     * `rdr_simulate_load_store()`: allocates the routing space for a load/store operation.
     * `rdr_apply_rotation_magic_state_from_*()`: applies a magic state from either
     *      memory or a fellow surface code qubit. The basic operation (a ZZ + X measurement)
     *      is the same in both cases, but the routing space allocated is different.
     * */
    bool rdr_simulate_store(Qubit*);
    bool rdr_apply_rotation_magic_state_from_surface_code(Qubit* target, Qubit* magic_state);
    bool rdr_apply_rotation_magic_state_from_memory(Qubit*);

    /*
     * `log_fidelity()` returns the natural log of the fidelity of the compute subsystem (no error
     * occurs during execution) for the given `Client*`. `scale` indicates the amount to scale
     * values such as the number of cycles by. `d_freq_khz` is the frequency of the `Driver`: note
     * that `Client::s_cycle_complete` is at the rate of `d_freq_khz`.
     * */
    double log_fidelity(Client*, double scale, double d_freq_khz, double phys_error) const; 

    const local_storage_type& local_memory() const { return local_memory_; }
    const production_level_type& t_factories() const { return t_factories_; }
    const memory_subsystem_type& memory_subsystem() const { return memory_subsystem_; }
    const local_storage_type& dedicated_ancilla() const { return dedicated_ancilla_; }

    size_t count_available_magic_states() const;
protected:
    long operate() override { return 1; }
private:
    execute_result_type do_h_gate(inst_ptr, Qubit*);
    execute_result_type do_s_like_gate(inst_ptr, Qubit*);
    execute_result_type do_cx_like_gate(inst_ptr, Qubit*, Qubit*);
    execute_result_type do_t_like_gate(inst_ptr, Qubit*);
    execute_result_type do_memory_access(inst_ptr, std::vector<Qubit*>);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#endif // SIM_COMPUTE_SUBSYSTEM_h
