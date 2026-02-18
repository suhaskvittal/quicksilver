/*
 *  author: Suhas Vittal
 *  date:   13 January 2026
 * */

#include "sim.h"
#include "sim/compute_base.h"
#include "sim/memory_subsystem.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Helper function declarations:
 * */

namespace
{

template <class ITER>
void _update_available_cycle(ITER begin, ITER end, cycle_type);

}  // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE_BASE::COMPUTE_BASE(std::string_view             name,
                           double                       freq_khz,
                           size_t                       _code_distance,
                           size_t                       _local_memory_capacity,
                           std::vector<PRODUCER_BASE*>  top_level_t_factories,
                           MEMORY_SUBSYSTEM*            memory_hierarchy)
    :OPERABLE(name, freq_khz),
    code_distance(_code_distance),
    local_memory_capacity(_local_memory_capacity),
    top_level_t_factories_(std::move(top_level_t_factories)),
    memory_hierarchy_(memory_hierarchy)
{
    // initialize local memory:
    local_memory_ = std::make_unique<STORAGE>(freq_khz,
                                            0,                      // n (does not matter)
                                            _local_memory_capacity, // k (matters)
                                            _code_distance,         // d (does not matter, but why not)
                                            _local_memory_capacity, // num_adapters 
                                            0,                      // load latency (0, we model with cycle available)
                                            0);                     // store latency (0, we model with cycle available)
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

const std::unique_ptr<STORAGE>&
COMPUTE_BASE::local_memory() const
{
    return local_memory_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

const std::vector<PRODUCER_BASE*>&
COMPUTE_BASE::top_level_t_factories() const
{
    return top_level_t_factories_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MEMORY_SUBSYSTEM*
COMPUTE_BASE::memory_hierarchy() const
{
    return memory_hierarchy_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE_BASE::execute_result_type
COMPUTE_BASE::execute_instruction(inst_ptr inst, std::array<QUBIT*, 3>&& args)
{
    if (is_software_instruction(inst->type))
        return execute_result_type{.progress=1, .latency=0};

    execute_result_type result{};
    switch (inst->type)
    {
    case INSTRUCTION::TYPE::H:
    case INSTRUCTION::TYPE::S:
    case INSTRUCTION::TYPE::SX:
    case INSTRUCTION::TYPE::SDG:
    case INSTRUCTION::TYPE::SXDG:
        result = do_h_or_s_gate(inst, args[0]);
        break;

    case INSTRUCTION::TYPE::CX:
    case INSTRUCTION::TYPE::CZ:
        result = do_cx_like_gate(inst, args[0], args[1]);
        break;

    case INSTRUCTION::TYPE::T:
    case INSTRUCTION::TYPE::TX:
    case INSTRUCTION::TYPE::TDG:
    case INSTRUCTION::TYPE::TXDG:
        result = do_t_like_gate(inst, args[0]);
        break;

    case INSTRUCTION::TYPE::LOAD:
    case INSTRUCTION::TYPE::STORE:
        result = do_memory_access(inst, args[0], inst->type == INSTRUCTION::TYPE::STORE);
        break;

    case INSTRUCTION::TYPE::COUPLED_LOAD_STORE:
        result = do_coupled_memory_access(inst, args[0], args[1]);
        break;

    default:
        std::cerr << "COMPUTE_BASE::execute_instruction: unknown instruction: " << *inst << _die{};
    }

    // update availability on success
    if (result.progress > 0)
        _update_available_cycle(args.begin(), args.begin()+inst->qubit_count, current_cycle()+result.latency);
    return result;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE_BASE::execute_result_type
COMPUTE_BASE::do_h_or_s_gate(inst_ptr inst, QUBIT* q)
{
    return execute_result_type{.progress=1, .latency=2*code_distance};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE_BASE::execute_result_type
COMPUTE_BASE::do_cx_like_gate(inst_ptr inst, QUBIT* q1, QUBIT* q2)
{
    return execute_result_type{.progress=1, .latency=2*code_distance};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE_BASE::execute_result_type
COMPUTE_BASE::do_t_like_gate(inst_ptr inst, QUBIT* q)
{
    // search for an available magic state:
    auto f_it = std::find_if(top_level_t_factories_.begin(), top_level_t_factories_.end(),
                        [] (const auto* f) { return f->buffer_occupancy() > 0; });
    if (f_it == top_level_t_factories_.end())
        return execute_result_type{};

    (*f_it)->consume(1);
    cycle_type latency;
    if (GL_ZERO_LATENCY_T_GATES)
        latency = 0;
    else if (GL_T_GATE_DO_AUTOCORRECT)
        latency = 2*code_distance;
    else
        latency = (GL_RNG() & 1) ? 4*code_distance : 2*code_distance;
    s_t_gates++;
    return execute_result_type{.progress=1, .latency=latency};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE_BASE::execute_result_type
COMPUTE_BASE::do_memory_access(inst_ptr inst, QUBIT* q, bool is_store)
{
    if (!is_store && local_memory_->contents().size() >= local_memory_capacity)
        return execute_result_type{};

    auto result = is_store 
                    ? memory_hierarchy_->do_store(q, current_cycle(), freq_khz)
                    : memory_hierarchy_->do_load(q, current_cycle(), freq_khz);
    if (result.success)
    {
        // need to convert storage latency to compute cycles:
        auto local_result = is_store
                            ? local_memory_->do_load(q)
                            : local_memory_->do_store(q);
        if (!local_result.success)
        {
            std::cerr << "COMPUTE_BASE::do_memory_access (store=" << is_store << "): local memory access failed.\n";
            local_memory_->print_adapter_debug_info(std::cerr);
            std::cerr << _die{};
        }

        auto total_latency = result.critical_latency + code_distance;   // add d cycles for data movement overhead
        return execute_result_type{.progress=1, .latency=total_latency};
    }
    return execute_result_type{};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE_BASE::execute_result_type
COMPUTE_BASE::do_coupled_memory_access(inst_ptr inst, QUBIT* ld, QUBIT* st)
{
    auto result = memory_hierarchy_->do_coupled_load_store(ld, st, current_cycle(), freq_khz);
    if (result.success)
    {
        auto local_result = local_memory_->do_coupled_load_store(st, ld);
        if (!local_result.success)
        {
            std::cerr << "COMPUTE_BASE::do_coupled_memory_access: local memory access failed.\n";
            local_memory_->print_adapter_debug_info(std::cerr);
            std::cerr << _die{};
        }

        auto total_latency = result.critical_latency + code_distance;
        return execute_result_type{.progress=1, .latency=total_latency};
    }
    return execute_result_type{};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE_BASE::execute_result_type
COMPUTE_BASE::do_rotation_gate_with_teleportation(inst_ptr inst, QUBIT* q, size_t max_teleports)
{
    return do_rotation_gate_with_teleportation_while_predicate_holds(inst, q, max_teleports,
                [] (const auto*, const auto*) { return true; });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
COMPUTE_BASE::count_available_magic_states() const
{
    return std::transform_reduce(top_level_t_factories_.begin(), top_level_t_factories_.end(),
                                size_t{0},
                                std::plus<size_t>{},
                                [] (const auto* f) { return f->buffer_occupancy(); });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/* HELPER FUNCTION DEFINITIONS START HERE */

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class ITER> void
_update_available_cycle(ITER begin, ITER end, cycle_type c)
{
    std::for_each(begin, end, [c] (auto* q) { q->cycle_available = c; });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim
