/*
 *  author: Suhas Vittal
 *  date:   11 March 2026
 * */

#include "sim/compute_subsystem.h"
#include "sim.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using execute_result_type = COMPUTE_SUBSYSTEM::execute_result_type;
using routing_type = COMPUTE_SUBSYSTEM::routing_type;
using r_id_type = routing_type::id_type;

size_t _dedicated_ancilla_count();
size_t _num_routing_channels(COMPUTE_SUBSYSTEM*);
size_t _channel_width(COMPUTE_SUBSYSTEM*);

size_t _get_idx_in_array(QUBIT*, const std::vector<QUBIT*>&);

/*
 * Calls `test_resources_between()` for both of `routing::MCB_LEFT_ENTRY` and
 * `routing::MCB_RIGHT_ENTRY`, and returns the first one that can be locked. 
 * Return std::nullopt if neither are available.
 * */
template <class SRC_TYPE>
std::optional<r_id_type> _test_endpoints_and_return_first_lockable(routing_type&,
                                                                    SRC_TYPE src,
                                                                    cycle_type from,
                                                                    cycle_type to);

/*
 * This function calls `LOCK_PRED` to test if if a given routing resource is available from
 * `start` to `start+delta`. If this fails, then the function tries again with `start+delta`
 * to `start+2*delta` and so on until a success occurs. This function terminates when `start_max`
 * is hit.
 *
 * If a success occurs, then this function returns the starting cycle that works. Otherwise,
 * `std::nullopt` is returned.
 * */
template <class LOCK_PRED>
std::optional<cycle_type> _test_multiple_time_intervals(cycle_type start, 
                                                        cycle_type delta,
                                                        cycle_type start_max,
                                                        const LOCK_PRED&);

template <class SRC_TYPE, class DST_TYPE>
cycle_type _get_earliest_lockable_time_between(routing_type&, 
                                                SRC_TYPE, 
                                                DST_TYPE, 
                                                cycle_type current_cycle, 
                                                cycle_type lock_duration);

template <class SRC_TYPE>
cycle_type _get_earliest_lockable_time_for_endpoints(routing_type&, 
                                                        SRC_TYPE, 
                                                        cycle_type current_cycle, 
                                                        cycle_type lock_duration);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE_SUBSYSTEM::routing_type::routing_type(COMPUTE_SUBSYSTEM* _c)
    :MULTI_CHANNEL_BUS(_num_routing_channels(_c), _channel_width(_c)),
    c(_c)
{}

COMPUTE_SUBSYSTEM::routing_type::id_type
COMPUTE_SUBSYSTEM::routing_type::translate(QUBIT* q) const
{
    // we translate the storage so that the dedicated ancilla
    // have priority access to magic state production (closer to 0).
    if (q->client_id == RDR_CLIENT_ID)
        return _get_idx_in_array(q, c->dedicated_ancilla());
    else
        return c->dedicated_ancilla_count + _get_idx_in_array(q, c->local_memory());
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
    dedicated_ancilla_count(_dedicated_ancilla_count()),
    local_memory_(local_memory_capacity),
    t_factories_(t_factories),
    memory_subsystem_(memory_subsystem),
    routing_(this),
    dedicated_ancilla_(dedicated_ancilla_count)
{
    // define routing space
    for (size_t i = 0; i < local_memory_capacity + dedicated_ancilla_count; i++)
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

    // we can initialize the dedicated qubits now:
    if (GL_RDR_ENABLED)
    {
        assert(dedicated_ancilla_count == GL_RDR_CAPACITY);
        for (int i = 0; i < GL_RDR_CAPACITY; i++)
        {
            QUBIT* q = new QUBIT{.qubit_id=i, .client_id=RDR_CLIENT_ID};
            dedicated_ancilla_[i] = q;
        }
    }
    else
    {
        assert(dedicated_ancilla_count == 0);
    }
}

COMPUTE_SUBSYSTEM::~COMPUTE_SUBSYSTEM()
{
    for (auto* q : dedicated_ancilla_)
        delete q;
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

execute_result_type
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

        s_inst_executed_by_type[static_cast<int>(inst->type)]++;
    }

    return result;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

execute_result_type
COMPUTE_SUBSYSTEM::do_rotation_via_rltp(inst_ptr inst, QUBIT* q, size_t remaining)
{
    assert(is_rotation_instruction(inst->type));
    assert(inst->uops_retired() < inst->uop_count());

    // can only do this if the current uop is a non-clifford gate
    const bool first_uop_is_a_non_clifford = is_t_like_instruction(inst->current_uop()->type);
    auto result = execute_instruction(inst->current_uop(), {q});
    if (result.progress == 0)
        return result;

    if (inst->retire_current_uop() || !first_uop_is_a_non_clifford)
    {
        q->cycle_available = current_cycle()+result.latency;
        q->last_operation_was_memory_access = false;
        return result;
    }

    // now, we need to handle the aspect of routing the XX and ZZ pauli-product measurements
    // from the program qubit to the EPR qubit.
    // 
    // This involves moving out of this channel (so we need to route to `MCB_LEFT_ENTRY`
    // or `MCB_RIGHT_ENTRY`). Assume that this routing problem is solved properly in practice,
    // and choose whichever is free
    cycle_type tp_start = current_cycle() + code_distance,
               tp_end = current_cycle() + 3*code_distance;
    auto channel_out = _test_endpoints_and_return_first_lockable(routing_, q, tp_start, tp_end);
    if (!channel_out.has_value())
        return result;

    routing_.lock_resources_between(q, *channel_out, tp_start, tp_end);
    // update the result latency, which is currently `code_distance + GL_REACTION_TIME + 1`.
    // The latter part (`GL_REACTION_TIME+1`) overlaps with the ZZ and XX measurements required
    // to teleport the program qubit
    result.latency = 3*code_distance; 
    while (inst->uops_retired() < inst->uop_count() && remaining > 0)
    {
        auto* uop = inst->current_uop();
        // operation occurs on an ancilla: assume operation always succeeds
        if (is_t_like_instruction(uop->type))
        {
            auto f_it = std::find_if(t_factories_.begin(), t_factories_.end(),
                                    [] (const auto* f) { return f->buffer_occupancy() > 0; });
            if (f_it == t_factories_.end())
                break;
            (*f_it)->consume(1);
            result.latency += GL_REACTION_TIME;
            remaining--;
        }
        result.progress++;
        
        // update stats:
        s_inst_executed_by_type[static_cast<int>(uop->type)]++;

        // retire uop:
        inst->retire_current_uop();
    }
    q->cycle_available = current_cycle()+result.latency;
    q->last_operation_was_memory_access = false;
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

/*
 * RDR generally has poor priority when it comes to routing space,
 * so we will need to allocate a space in advance.
 * */

bool
COMPUTE_SUBSYSTEM::rdr_simulate_store(QUBIT* q)
{
    const size_t d = code_distance;
    const cycle_type c = _get_earliest_lockable_time_for_endpoints(routing_, q, current_cycle(), d);

    auto dst = _test_endpoints_and_return_first_lockable(routing_, q, c, c+d);
    if (!dst.has_value())
        return false;
    // lock routing space and return true:
    routing_.lock_resources_between(q, *dst, c, c+d);
    q->cycle_available = c+d+1;  // +1 due to destruction by X measurement
    return true;
}

bool
COMPUTE_SUBSYSTEM::rdr_apply_rotation_magic_state_from_surface_code(QUBIT* q, QUBIT* m)
{
    const size_t d = code_distance;
    const cycle_type c = _get_earliest_lockable_time_between(routing_, q, m, current_cycle(), d);

    if (!routing_.test_resources_between(q, m, c, c+d))
        return false;
    routing_.lock_resources_between(q, m, c, c+d);
    q->cycle_available = c+d;
    m->cycle_available = c+d+1;
    return true;
}

bool
COMPUTE_SUBSYSTEM::rdr_apply_rotation_magic_state_from_memory(QUBIT* q)
{
    const size_t d = code_distance;
    const cycle_type c = _get_earliest_lockable_time_for_endpoints(routing_, q, current_cycle(), d);

    auto dst = _test_endpoints_and_return_first_lockable(routing_, q, c, c+d);
    if (!dst.has_value())
        return false;
    // lock routing space and return true:
    routing_.lock_resources_between(q, *dst, c, c+d);
    q->cycle_available = c+d;
    return true;
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

const COMPUTE_SUBSYSTEM::local_storage_type&
COMPUTE_SUBSYSTEM::dedicated_ancilla() const
{
    return dedicated_ancilla_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

execute_result_type
COMPUTE_SUBSYSTEM::do_h_gate(inst_ptr inst, QUBIT* q)
{
    return execute_result_type{.progress=1, .latency=1};  // can be done transversally (up-to a 45deg rotation)
}

execute_result_type
COMPUTE_SUBSYSTEM::do_s_like_gate(inst_ptr inst, QUBIT* q)
{
    if (routing_.test_local_resource(q, current_cycle(), current_cycle()+code_distance))
    {
        routing_.lock_local_resource(q, current_cycle(), current_cycle()+code_distance);
        return execute_result_type{.progress=1, .latency=code_distance};
    }
    return execute_result_type{};
}

execute_result_type
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

execute_result_type
COMPUTE_SUBSYSTEM::do_t_like_gate(inst_ptr inst, QUBIT* q)
{
    // Once the X/Y measurement complete, it will take a software decoder about 1us per round to
    // determine a result. So, we assume the reaction time is the code distance (assuming each round/cycle
    // takes 1us)
    const cycle_type t_start = current_cycle(),
                     t_end = current_cycle() + code_distance;

    // two parts: (1) ZZ measurement with magic state, and (2) S correction

    // first get factory with magic state:
    auto f_it = std::find_if(t_factories_.begin(), t_factories_.end(),
                            [] (const auto* f) { return f->buffer_occupancy() > 0; });
    if (f_it == t_factories_.end())
        return execute_result_type{};

    // get routing space -- on success, we can execute the T gate
    auto dst = _test_endpoints_and_return_first_lockable(routing_, q, t_start, t_end);
    if (!dst.has_value())
        return execute_result_type{};

    (*f_it)->consume(1);
    routing_.lock_resources_between(q, *dst, t_start, t_end);
    cycle_type latency = code_distance + GL_REACTION_TIME;
    return execute_result_type{.progress=1, .latency=latency};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

execute_result_type
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
    cycle_type mv_start = current_cycle(),
               mv_end = current_cycle() + 2*code_distance;
    auto dst = _test_endpoints_and_return_first_lockable(routing_, st, mv_start, mv_end);
    if (!dst.has_value())
        return execute_result_type{};

    // find memory location that contains this memory:
    MEMORY_LEVEL* m = memory_subsystem_[memory_level_map_[ld]];
    MEMORY_ACCESS_RESULT result = m->do_coupled_load_store(ld, st);
    if (!result.success)
        return execute_result_type{};

    // update data structures
    routing_.lock_resources_between(st, *dst, mv_start, mv_end);
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

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
_num_routing_channels(COMPUTE_SUBSYSTEM* c)
{
    return 1;
}

size_t
_channel_width(COMPUTE_SUBSYSTEM* c)
{
    size_t q_count = c->dedicated_ancilla_count + c->local_memory_capacity;
    if (q_count & 1)
        q_count++;
    size_t w = q_count >> 1;
    return w;
}

size_t
_dedicated_ancilla_count()
{
    if (GL_RDR_ENABLED)
        return GL_RDR_CAPACITY;
    else
        return 0;
}

size_t
_get_idx_in_array(QUBIT* q, const std::vector<QUBIT*>& arr)
{
    auto it = std::find(arr.begin(), arr.end(), q);
    assert(it != arr.end());
    return std::distance(arr.begin(), it);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T> std::optional<r_id_type>
_test_endpoints_and_return_first_lockable(routing_type& r, T src, cycle_type from, cycle_type to)
{
    for (r_id_type dst : {routing::MCB_LEFT_ENTRY, routing::MCB_RIGHT_ENTRY})
        if (r.test_resources_between(src, dst, from, to))
            return std::make_optional(dst);
    return std::nullopt;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class LOCK_PRED> std::optional<cycle_type>
_test_multiple_time_intervals(cycle_type start, cycle_type delta, cycle_type start_max, const LOCK_PRED& pred)
{
    while (start < start_max)
    {
        if (pred(start, start+delta))
            return std::make_optional(start);
        start += delta;
    }

    return std::nullopt;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T, class U> cycle_type
_get_earliest_lockable_time_between(routing_type& r, T src, U dst, cycle_type c, cycle_type d)
{
    cycle_type out{0};
    r.for_each_resource_between(src, dst,
            [&out, c, d] (const auto& res)
            {
                out = std::max(out, res.next_ready_cycle(c, d));
            });
    return out;
}

template <class T> cycle_type
_get_earliest_lockable_time_for_endpoints(routing_type& r, T src, cycle_type c, cycle_type d)
{
    cycle_type earliest{std::numeric_limits<cycle_type>::max()};
    for (r_id_type dst : {routing::MCB_LEFT_ENTRY, routing::MCB_RIGHT_ENTRY})
        earliest = std::min(earliest, _get_earliest_lockable_time_between(r, src, dst, c, d));
    return earliest;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim
