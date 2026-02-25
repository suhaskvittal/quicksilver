/*
 *  author: Suhas Vittal
 *  date:   13 January 2026
 * */

#ifndef SIM_COMPUTE_BASE_h
#define SIM_COMPUTE_BASE_h

#include "sim/client.h"
#include "sim/production/magic_state.h"
#include "sim/storage.h"

#include <array>
#include <memory>
#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class MEMORY_SUBSYSTEM;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class COMPUTE_BASE : public OPERABLE
{
public:
    using inst_ptr = CLIENT::inst_ptr;
    using production_level_type = std::vector<PRODUCER_BASE*>;

    /*
     * Results of `execute_instruction()`
     * */
    struct execute_result_type
    {
        long       progress{0};
        cycle_type latency;
    };

    uint64_t s_t_gates{0};
    uint64_t s_t_gate_teleports{0};
    uint64_t s_t_gate_teleport_episodes{0};

    const size_t code_distance;
    const size_t local_memory_capacity;
protected:
    std::unique_ptr<STORAGE> local_memory_;
    production_level_type    top_level_t_factories_;
    MEMORY_SUBSYSTEM*        memory_hierarchy_;
public:
    COMPUTE_BASE(std::string_view      name, 
                 double                freq_khz,
                 size_t                code_distance,
                 size_t                local_memory_capacity,
                 production_level_type top_level_t_factories,
                 MEMORY_SUBSYSTEM*     memory_hierarchy);

    const std::unique_ptr<STORAGE>& local_memory() const;
    const production_level_type&    top_level_t_factories() const;
    MEMORY_SUBSYSTEM*               memory_hierarchy() const;
protected:
    virtual execute_result_type execute_instruction(inst_ptr, std::array<QUBIT*, 3>&& args);

    virtual execute_result_type do_h_or_s_gate(inst_ptr, QUBIT*);
    virtual execute_result_type do_cx_like_gate(inst_ptr, QUBIT* ctrl, QUBIT* target);
    virtual execute_result_type do_t_like_gate(inst_ptr, QUBIT*);
    virtual execute_result_type do_memory_access(inst_ptr, QUBIT*, bool is_store);
    virtual execute_result_type do_coupled_memory_access(inst_ptr, QUBIT*, QUBIT*);

    /*
     * This is a generic function as this effectively replaces the whole loop of calling
     * `execute_instruction()` on each uop of a rotation gate.
     *
     * This function will continue to retire uops for a rotation gate until `max_teleports`
     * expires or `loop_pred()` becomes false. Every iteration, `ITER_CALLBACK` is called.
     * Before a uop gets retired, `RETIRE_CALLBACK` is called.
     *
     * All functions should take in two arguments: (1) the original instruction, and
     * (2) the current uop for that instruction.
     *
     * Only `loop_pred` needs to return anything (a bool).
     * */
    template <class PRED, class ITER_CALLBACK, class RETIRE_CALLBACK> 
    execute_result_type do_rotation_gate_with_teleportation(inst_ptr,
                                                            QUBIT*,
                                                            size_t max_teleports,
                                                            const PRED& loop_pred,
                                                            const ITER_CALLBACK&,
                                                            const RETIRE_CALLBACK&);

    size_t count_available_magic_states() const;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Implementation of `do_rotation_gate_with_teleportation_while_predicate_holds`
 * */

template <class PRED, class ITER_CALLBACK, class RETIRE_CALLBACK> 
COMPUTE_BASE::execute_result_type
COMPUTE_BASE::do_rotation_gate_with_teleportation(inst_ptr inst, 
                                                    QUBIT* q, 
                                                    size_t tp_remaining, 
                                                    const PRED& loop_pred,
                                                    const ITER_CALLBACK& iter_callback,
                                                    const RETIRE_CALLBACK& retire_callback)
{
    if (!loop_pred(inst, inst->current_uop()))
        return execute_result_type{};
    iter_callback(inst, inst->current_uop());

    execute_result_type out = execute_instruction(inst->current_uop(), {q});
    if (out.progress)
    {
        retire_callback(inst, inst->current_uop());
        if (inst->retire_current_uop())
            return out;
    }
    else
    {
        return out;
    }

    bool any_teleports{false};
    while (tp_remaining > 0 && loop_pred(inst, inst->current_uop()))
    {
        iter_callback(inst, inst->current_uop());

        auto result = execute_instruction(inst->current_uop(), {q});
        if (result.progress)
        {
            if (is_t_like_instruction(inst->current_uop()->type))
            {
                tp_remaining--;
                s_t_gate_teleports++;
                if (!GL_T_GATE_DO_AUTOCORRECT)
                    out.latency += (GL_RNG() & 3) ? 2*code_distance : 0; // any possible correction incurs a 2-cycle latency
                any_teleports = true;
            }

            out.progress += result.progress;
            retire_callback(inst, inst->current_uop());
            if (inst->retire_current_uop())
                break;
        }
        else
        {
            break;
        }
    }

    if (any_teleports)
    {
        s_t_gate_teleport_episodes++;
        if (GL_T_GATE_DO_AUTOCORRECT)
            out.latency += 2*code_distance;
    }

    if (GL_ZERO_LATENCY_T_GATES)
        out.latency = 0;

    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim

#endif  // SIM_COMPUTE_BASE_h
