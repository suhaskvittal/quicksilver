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

class COMPUTE_SUBSYSTEM : public OPERABLE
{
public:
    using inst_ptr = INSTRUCTION*;
    using local_storage_type = std::vector<QUBIT*>;
    using production_level_type = std::vector<PRODUCER_BASE*>;
    using memory_subsystem_type = std::vector<MEMORY_LEVEL*>;
    
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
    struct routing_type : routing::MULTI_CHANNEL_BUS<routing_type>
    {
        using routing::MULTI_CHANNEL_BUS<routing_type>::id_type;

        COMPUTE_SUBSYSTEM* owner;

        routing_type(COMPUTE_SUBSYSTEM*);
        id_type translate(QUBIT*) const;
    };

    const size_t code_distance;
    const size_t local_memory_capacity;

    /*
     * Statistics:
     * */
private:
    local_storage_type local_memory_;
    production_level_type t_factories_;
    memory_subsystem_type memory_subsystem_;

    routing_type routing_;

    /*
     * `memory_level_map_` is used to accelerate lookups into the
     * `memory_subsystem_`. If a qubit has a idx of -1, then it
     * indexes into `local_memory_`.
     * */
    std::unordered_map<QUBIT*, ssize_t> memory_level_map_;
public:
    COMPUTE_SUBSYSTEM(double freq_khz, 
                        size_t code_distance, 
                        size_t local_memory_capacity,
                        production_level_type t_factories,
                        memory_subsystem_type memory_subsystem);

    void initialize_qubits(std::vector<QUBIT*> program_qubits);

    /*
     * Instruction execution: `inst_ptr` is the instruction to be executed,
     * and the second operand are the pointers to the qubits this instruction
     * operates on.
     * */
    execute_result_type execute_instruction(inst_ptr, std::vector<QUBIT*>);

    /*
     * Returns true if the qubit is in `local_memory_`
     * */
    bool is_qubit_in_local_memory(const QUBIT*) const;

    const local_storage_type& local_memory() const;
    const production_level_type& t_factories() const;
    const memory_subsystem_type& memory_subsystem() const;
protected:
    long operate() override { return 1; }
private:
    execute_result_type do_h_gate(inst_ptr, QUBIT*);
    execute_result_type do_s_like_gate(inst_ptr, QUBIT*);
    execute_result_type do_cx_like_gate(inst_ptr, QUBIT*, QUBIT*);
    execute_result_type do_t_like_gate(inst_ptr, QUBIT*);
    execute_result_type do_memory_access(inst_ptr, std::vector<QUBIT*>);

    size_t count_available_magic_states() const;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#endif // SIM_COMPUTE_SUBSYSTEM_h
