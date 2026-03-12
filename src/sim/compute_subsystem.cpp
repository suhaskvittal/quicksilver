/*
 *  author: Suhas Vittal
 *  date:   11 March 2026
 * */

#include "sim/compute_subsystem.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

size_t _num_routing_channels(COMPUTE_SUBSYSTEM*);
size_t _channel_width(COMPUTE_SUBSYSTEM*);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE_SUBSYSTEM::routing_type::routing_type(COMPUTE_SUBSYSTEM* cs)
    :MULTI_CHANNEL_BUS(_num_routing_channels(cs), _channel_width(cs)),
    owner(cs)
{}

COMPUTE_SUBSYSTEM::routing_type::id_type
COMPUTE_SUBSYSTEM::routing_type::translate(QUBIT* q) const
{
    auto begin = owner->local_memory().begin(),
         end = owner->local_memory().end();
    auto q_it = std::find(begin, end, q);
    assert(q_it != end);
    return std::distance(begin, q_it);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE_SUBSYSTEM::COMPUTE_SUBSYSTEM(double freq_khz,
                                      size_t _code_distance,
                                      size_t _local_memory_capacity,
                                      production_level_type t_factories,
                                      memory_subsystem_type memory_subsystem)
    :OPERABLE("compute_subsystem", freq_khz),
    code_distance(_code_distance),
    local_memory_capacity(_local_memory_capacity),
    local_memory_(local_memory_capacity),
    t_factories_(t_factories),
    memory_subsystem_(memory_subsystem),
    routing_(this)
{
    // define routing space
    for (size_t i = 0; i < local_memory_capacity; i++)
    {
        size_t ii{i};
        const int ch = ii % routing_.num_channels;
        ii /= routing_.num_channels;
        const int ro = ii & 1;
        ii >>= 1;
        const int co = ii;
        assert(co < routing_.channel_width);

        routing_.set_location(i, ch, ro, co);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE_SUBSYSTEM::initialize_qubits(std::vector<QUBIT*> program_qubits)
{
    for (size_t i = 0; i < local_memory_capacity; i++)
    {
        QUBIT* q = program_qubits[i];
        memory_level_map_[q] = -1;
        local_memory_[i] = q;
    }

    size_t begin_idx = local_memory_capacity;
    size_t level{0};
    for (auto* m : memory_subsystem_)
    {
        if (begin_idx == program_qubits.size())
            std::cerr << "COMPUTE_SUBSYSTEM::initialize_qubits: extraneous levels in memory subsystem" << _die{};

        size_t end_idx = std::min(program_qubits.size(), begin_idx + m->total_capacity);
        auto begin = program_qubits.begin() + begin_idx,
             end = program_qubits.begin() + end_idx;
        m->striped_mapping(begin, end);
        std::for_each(begin, end, [this, level] (QUBIT* q) { memory_level_map_[q] = level; });
        begin_idx = end_idx;
        level++;
    }


}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE_SUBSYSTEM::execute_result_type
COMPUTE_SUBSYSTEM::execute_instruction(inst_ptr inst, std::vector<QUBIT*> args)
{
    if (is_software_instruction(inst->type))
        return execute_result_type{.progress=1, .latency=0};

    execute_result_type result{};
    switch (inst->type)
    {
    case INSTRUCTION::TYPE::H:
        result = do_h_gate(inst, args[0]);
        break;

    case INSTRUCTION::TYPE::S:
    case INSTRUCTION::TYPE::SX:
    case INSTRUCTION::TYPE::SDG:
    case INSTRUCTION::TYPE::SXDG:
        result = do_s_like_gate(inst, args[0]);
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
    case INSTRUCTION::TYPE::COUPLED_LOAD_STORE:
        result = do_memory_access(inst, args);
        break;

    default:
        std::cerr << "COMPUTE_SUBSYSTEM::execute_instruction: unknown instruction: " << *inst << _die{};
    }

    // update availability on success
    if (result.progress > 0)
    {
        for (size_t i = 0; i < inst->qubit_count; i++)
        {
            auto* q = args[i];
            q->cycle_available = current_cycle() + result.latency;
            q->last_operation_was_memory_access = is_memory_access(inst->type);
        }
    }

    return result;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
COMPUTE_SUBSYSTEM::is_qubit_in_local_memory(const QUBIT* q) const
{
    auto it = std::find(local_memory_.begin(), local_memory_.end(), q);
    return it != local_memory_.end();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

const COMPUTE_SUBSYSTEM::local_storage_type&
COMPUTE_SUBSYSTEM::local_memory() const
{
    return local_memory_;
}

const COMPUTE_SUBSYSTEM::production_level_type&
COMPUTE_SUBSYSTEM::t_factories() const
{
    return t_factories_;
}

const COMPUTE_SUBSYSTEM::memory_subsystem_type&
COMPUTE_SUBSYSTEM::memory_subsystem() const
{
    return memory_subsystem_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE_SUBSYSTEM::execute_result_type
COMPUTE_SUBSYSTEM::do_h_gate(inst_ptr inst, QUBIT* q)
{
    return execute_result_type{.progress=1, .latency=1};  // can be done transversally (up-to a 45deg rotation)
}

COMPUTE_SUBSYSTEM::execute_result_type
COMPUTE_SUBSYSTEM::do_s_like_gate(inst_ptr inst, QUBIT* q)
{
    if (routing_.test_local_resource(q, current_cycle(), current_cycle()+code_distance))
    {
        routing_.lock_local_resource(q, current_cycle(), current_cycle()+code_distance);
        return execute_result_type{.progress=1, .latency=code_distance};
    }
    return execute_result_type{};
}

COMPUTE_SUBSYSTEM::execute_result_type
COMPUTE_SUBSYSTEM::do_cx_like_gate(inst_ptr inst, QUBIT* c, QUBIT* t)
{
    if (routing_.test_resources_between(c, t, current_cycle(), current_cycle() + 2*code_distance))
    {
        routing_.lock_resources_between(c, t, current_cycle(), current_cycle() + 2*code_distance);
        return execute_result_type{.progress=1, .latency=2*code_distance};
    }
    return execute_result_type{};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE_SUBSYSTEM::execute_result_type
COMPUTE_SUBSYSTEM::do_t_like_gate(inst_ptr inst, QUBIT* q)
{
    // Once the X/Y measurement complete, it will take a software decoder about 1us per round to
    // determine a result. So, we assume the reaction time is the code distance (assuming each round/cycle
    // takes 1us)
    const size_t reaction_time{code_distance};

    const cycle_type t_start = current_cycle(),
                     t_end = current_cycle() + code_distance,
                     s_start = current_cycle() + code_distance + reaction_time,
                     s_end = current_cycle() + 2*code_distance + reaction_time;

    // two parts: (1) ZZ measurement with magic state, and (2) S correction

    // first get factory with magic state:
    auto f_it = std::find_if(t_factories_.begin(), t_factories_.end(),
                            [] (const auto* f) { return f->buffer_occupancy() > 0; });
    if (f_it == t_factories_.end())
        return execute_result_type{};

    // depending on whether S correction is needed, we will need space for routing.
    // check if routing space can be consumed -- we need to allocate for the worst case
    bool ok = routing_.test_resources_between(q, routing::MCB_LEFT_ENTRY, t_start, t_end);
    ok &= routing_.test_local_resource(q, s_start, s_end);

    if (!ok)
        return execute_result_type{};

    // ZZ measurement resolves, now we know for sure if the S correction is needed
    const bool s_correction_needed = (GL_RNG() & 1) > 0;

    (*f_it)->consume(1);
    routing_.lock_resources_between(q, routing::MCB_LEFT_ENTRY, t_start, t_end);
    if (s_correction_needed)
        routing_.lock_local_resource(q, s_start, s_end);

    cycle_type latency = code_distance + reaction_time;
    if (s_correction_needed)
        latency += code_distance;
    return execute_result_type{.progress=1, .latency=latency};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE_SUBSYSTEM::execute_result_type
COMPUTE_SUBSYSTEM::do_memory_access(inst_ptr inst, std::vector<QUBIT*> args)
{
    if (inst->type == INSTRUCTION::TYPE::LOAD || inst->type == INSTRUCTION::TYPE::STORE)
    {
        std::cerr << "COMPUTE_SUBSYSTEM::do_memory_access: memory access of type "
                << BASIS_GATES[static_cast<int>(inst->type)] << " currently unsupported" << _die{};
    }

    QUBIT* ld = args[0];
    QUBIT* st = args[1];

    assert(memory_level_map_[ld] >= 0 && memory_level_map_[st] < 0);

    // consume routing space on the compute subsystem side:
    // d cycles to move out `st` and d cycles for transfering `ld` into its place.
    bool ok = routing_.test_resources_between(st, 
                                                routing::MCB_RIGHT_ENTRY, 
                                                current_cycle(), 
                                                current_cycle()+2*code_distance);
    if (!ok)
        return execute_result_type{};

    // find memory location that contains this memory:
    MEMORY_LEVEL* m = memory_subsystem_[memory_level_map_[ld]];
    MEMORY_ACCESS_RESULT result = m->do_coupled_load_store(ld, st);
    if (!result.success)
        return execute_result_type{};

    // update data structures
    routing_.lock_resources_between(st, routing::MCB_RIGHT_ENTRY, current_cycle(), current_cycle()+2*code_distance);
    memory_level_map_[st] = memory_level_map_[ld];
    memory_level_map_[ld] = -1;
    
    auto q_it = std::find(local_memory_.begin(), local_memory_.end(), st);
    *q_it = ld;

    execute_result_type out{.progress=1};
    out.latency = convert_cycles_between_frequencies(result.latency, result.freq_khz, freq_khz);
    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
COMPUTE_SUBSYSTEM::count_available_magic_states() const
{
    return std::transform_reduce(t_factories_.begin(), t_factories_.end(), size_t{0}, std::plus<size_t>{},
                                [] (const auto* f) { return f->buffer_occupancy(); });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

size_t
_num_routing_channels(COMPUTE_SUBSYSTEM* c)
{
    return 1;
}

size_t
_channel_width(COMPUTE_SUBSYSTEM* c)
{
    return c->local_memory_capacity / 2;
}

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim
