/*
 *  author: Suhas Vittal
 *  date:   11 March 2026
 * */

#include "sim/client.h"
#include "sim/compute_subsystem.h"
#include "sim/configuration/resource_estimation.h"
#include "sim.h"

//#define LOG_INST

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using execute_result_type = ComputeSubsystem::execute_result_type;
using routing_type = ComputeSubsystem::routing_type;
using r_id_type = routing_type::id_type;

constexpr size_t MAX_QUBITS_PER_CHANNEL{8};

size_t _dedicated_ancilla_count();
size_t _num_routing_channels(ComputeSubsystem*);
size_t _channel_width(ComputeSubsystem*);

size_t _get_idx_in_array(Qubit*, const std::vector<Qubit*>&);

/*
 * Calls `test_resources_between()` for both of `routing::MCB_LEFT_ENTRY` and
 * `routing::MCB_RIGHT_ENTRY`, and returns the first one that can be locked. 
 * Return std::nullopt if neither are available.
 * */
template <class Src>
std::optional<r_id_type> _test_endpoints_and_return_first_lockable(routing_type&,
                                                                    Src src,
                                                                    cycle_type from,
                                                                    cycle_type to);

/*
 * This function calls `LockPred` to test if if a given routing resource is available from
 * `start` to `start+delta`. If this fails, then the function tries again with `start+delta`
 * to `start+2*delta` and so on until a success occurs. This function terminates when `start_max`
 * is hit.
 *
 * If a success occurs, then this function returns the starting cycle that works. Otherwise,
 * `std::nullopt` is returned.
 * */
template <class LockPred>
std::optional<cycle_type> _test_multiple_time_intervals(cycle_type start, 
                                                        cycle_type delta,
                                                        cycle_type start_max,
                                                        const LockPred&);

template <class Src, class Dst>
cycle_type _get_earliest_lockable_time_between(routing_type&, 
                                                Src, 
                                                Dst, 
                                                cycle_type current_cycle, 
                                                cycle_type lock_duration);

template <class Src>
cycle_type _get_earliest_lockable_time_for_endpoints(routing_type&, 
                                                        Src, 
                                                        cycle_type current_cycle, 
                                                        cycle_type lock_duration);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ComputeSubsystem::routing_type::routing_type(ComputeSubsystem* _c)
    :MultiChannelBus(_num_routing_channels(_c), _channel_width(_c)),
    c(_c)
{}

ComputeSubsystem::routing_type::id_type
ComputeSubsystem::routing_type::translate(Qubit* q) const
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

ComputeSubsystem::ComputeSubsystem(double freq_khz,
                                      size_t _code_distance,
                                      size_t _local_memory_capacity,
                                      production_level_type t_factories,
                                      memory_subsystem_type memory_subsystem)
    :Operable("compute_subsystem", freq_khz),
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
            Qubit* q = new Qubit{.qubit_id=i, .client_id=RDR_CLIENT_ID};
            dedicated_ancilla_[i] = q;
        }
    }
    else
    {
        assert(dedicated_ancilla_count == 0);
    }
}

ComputeSubsystem::~ComputeSubsystem()
{
    for (auto* q : dedicated_ancilla_)
        delete q;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
ComputeSubsystem::initialize_qubits(std::vector<Qubit*> program_qubits)
{
    for (size_t i = 0; i < local_memory_capacity; i++)
    {
        Qubit* q = program_qubits[i];
        memory_level_map_[q] = -1;
        local_memory_[i] = q;
    }

    size_t begin_idx = local_memory_capacity;
    size_t level{0};
    for (auto* m : memory_subsystem_)
    {
        if (begin_idx == program_qubits.size())
            std::cerr << "ComputeSubsystem::initialize_qubits: extraneous levels in memory subsystem" << _die{};

        size_t end_idx = std::min(program_qubits.size(), begin_idx + m->total_capacity);
        auto begin = program_qubits.begin() + begin_idx,
             end = program_qubits.begin() + end_idx;
        m->striped_mapping(begin, end);
        std::for_each(begin, end, [this, level] (Qubit* q) { memory_level_map_[q] = level; });
        begin_idx = end_idx;
        level++;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

execute_result_type
ComputeSubsystem::execute_instruction(inst_ptr inst, std::vector<Qubit*> args)
{
    if (is_software_instruction(inst->type))
        return execute_result_type{.progress=1, .latency=0};

    execute_result_type result{};
    switch (inst->type)
    {
    case Instruction::Type::H:
        result = do_h_gate(inst, args[0]);
        break;

    case Instruction::Type::S:
    case Instruction::Type::SX:
    case Instruction::Type::SDG:
    case Instruction::Type::SXDG:
        result = do_s_like_gate(inst, args[0]);
        break;

    case Instruction::Type::CX:
    case Instruction::Type::CZ:
        result = do_cx_like_gate(inst, args[0], args[1]);
        break;

    case Instruction::Type::T:
    case Instruction::Type::TX:
    case Instruction::Type::TDG:
    case Instruction::Type::TXDG:
        result = do_t_like_gate(inst, args[0]);
        break;

    case Instruction::Type::LOAD:
    case Instruction::Type::STORE:
    case Instruction::Type::COUPLED_LOAD_STORE:
        result = do_memory_access(inst, args);
        break;

    default:
        std::cerr << "ComputeSubsystem::execute_instruction: unknown instruction: " << *inst << _die{};
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
ComputeSubsystem::do_rotation_via_rltp(inst_ptr inst, Qubit* q, size_t remaining)
{
    assert(is_rotation_instruction(inst->type));
    assert(inst->uops_retired() < inst->uop_count());

    // can only do this if the current uop is a non-clifford gate
    const bool first_uop_is_a_non_clifford = is_t_like_instruction(inst->current_uop()->type);
    auto result = execute_instruction(inst->current_uop(), {q});
    if (result.progress == 0)
        return result;

    if (inst->retire_current_uop() || !first_uop_is_a_non_clifford || current_cycle() < rltp_ready_cycle_)
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

    // update the result latency which is incorrect
    result.latency = 3*code_distance + GL_REACTION_TIME;  // ZZ with |T> + teleporting XX and ZZ + feedforward
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
            // need one cycle to measure |Y> ancilla
            result.latency += 1 + GL_REACTION_TIME;  
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

    // the RLTP resuorces become ready after 4d cycles:
    //  (1) bell state prep (d)
    //  (2) the 3d from the actual RLTP procedure
    rltp_ready_cycle_ = current_cycle() + 4*code_distance;

    return result;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
ComputeSubsystem::is_qubit_in_local_memory(const Qubit* q) const
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
ComputeSubsystem::rdr_simulate_store(Qubit* q)
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
ComputeSubsystem::rdr_apply_rotation_magic_state_from_surface_code(Qubit* q, Qubit* m)
{
    const size_t d = code_distance;
    const cycle_type c = _get_earliest_lockable_time_between(routing_, q, m, current_cycle(), d);
    if (!routing_.test_resources_between(q, m, c, c+d))
        return false;
    routing_.lock_resources_between(q, m, c, c+d);
    q->cycle_available = c + d + GL_REACTION_TIME;
    m->cycle_available = c + d + 1;
    return true;
}

bool
ComputeSubsystem::rdr_apply_rotation_magic_state_from_memory(Qubit* q)
{
    const size_t d = code_distance;
    const cycle_type c = _get_earliest_lockable_time_for_endpoints(routing_, q, current_cycle(), d);
    auto dst = _test_endpoints_and_return_first_lockable(routing_, q, c, c+d);
    if (!dst.has_value())
        return false;
    // lock routing space and return true:
    routing_.lock_resources_between(q, *dst, c, c+d);
    q->cycle_available = c + d + GL_REACTION_TIME;
    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

double
ComputeSubsystem::log_fidelity(Client* c, double scale, double d_freq_khz, double p) const
{
    const double cycles = convert_cycles_between_frequencies(c->s_cycle_complete, d_freq_khz, freq_khz) * scale;
    const double ler_per_d_cycles = configuration::surface_code_logical_error_rate(code_distance, p);
    const double t_gates = c->s_t_gates_done * scale;
    const double t_infidelity = t_factories_[0]->output_error_probability;

    // assert that all `t_factories_` have the same fidelity:
    const bool all_fact_have_same_fidelity = std::all_of(t_factories_.begin(), t_factories_.end(),
                                                [x=t_infidelity] (const auto* f)
                                                {
                                                    return std::abs(x - f->output_error_probability) < 1e-12;
                                                });
    if (!all_fact_have_same_fidelity)
        std::cerr << "ComputeSubsystem::log_fidelity: T factories do not have similar fidelities" << _die{};

    // memory (idle) fidelity
    const double log_f_mem = local_memory_capacity * fpdiv(cycles, code_distance) * std::log(1 - ler_per_d_cycles);
    // T gate fidelity
    const double log_f_t = t_gates * std::log(1.0 - t_infidelity);
    // total fidelity:
    const double log_f = log_f_mem + log_f_t;
    return log_f;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
ComputeSubsystem::count_available_magic_states() const
{
    return std::transform_reduce(t_factories_.begin(), t_factories_.end(), size_t{0}, std::plus<size_t>{},
                                [] (const auto* f) { return f->buffer_occupancy(); });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

execute_result_type
ComputeSubsystem::do_h_gate(inst_ptr inst, Qubit* q)
{
    return execute_result_type{.progress=1, .latency=1};  // can be done transversally (up-to a 45deg rotation)
}

execute_result_type
ComputeSubsystem::do_s_like_gate(inst_ptr inst, Qubit* q)
{
    if (routing_.test_local_resource(q, current_cycle(), current_cycle()+code_distance))
    {
        routing_.lock_local_resource(q, current_cycle(), current_cycle()+code_distance);
        return execute_result_type{.progress=1, .latency=code_distance};
    }
    return execute_result_type{};
}

execute_result_type
ComputeSubsystem::do_cx_like_gate(inst_ptr inst, Qubit* c, Qubit* t)
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
ComputeSubsystem::do_t_like_gate(inst_ptr inst, Qubit* q)
{
    // first get factory with magic state:
    auto f_it = std::find_if(t_factories_.begin(), t_factories_.end(),
                            [] (const auto* f) { return f->buffer_occupancy() > 0; });
    if (f_it == t_factories_.end())
        return execute_result_type{};

    // Once the X/Y measurement complete, it will take a software decoder about 1us per round to
    // determine a result. So, we assume the reaction time is the code distance (assuming each round/cycle
    // takes 1us)
    const cycle_type t_start = current_cycle(),
                     t_end = current_cycle() + code_distance;
    // get routing space -- on success, we can execute the T gate
    auto dst = _test_endpoints_and_return_first_lockable(routing_, q, t_start, t_end);
    if (!dst.has_value())
        return execute_result_type{};
    routing_.lock_resources_between(q, *dst, t_start, t_end);

    // estimate decoding latency:
    const cycle_type total_latency = code_distance + GL_REACTION_TIME;
    (*f_it)->consume(1);
    return execute_result_type{.progress=1, .latency=total_latency};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

execute_result_type
ComputeSubsystem::do_memory_access(inst_ptr inst, std::vector<Qubit*> args)
{
    if (inst->type == Instruction::Type::LOAD || inst->type == Instruction::Type::STORE)
    {
        std::cerr << "ComputeSubsystem::do_memory_access: memory access of type "
                << BASIS_GATES[static_cast<int>(inst->type)] << " currently unsupported" << _die{};
    }

    Qubit* ld = args[0];
    Qubit* st = args[1];

    assert(memory_level_map_[ld] >= 0 && memory_level_map_[st] < 0);

    // consume routing space on the compute subsystem side:
    // d cycles to move out `st` and d cycles for transfering `ld` into its place.
    cycle_type mv_start = current_cycle(),
               mv_end = current_cycle() + 2*code_distance;
    auto dst = _test_endpoints_and_return_first_lockable(routing_, st, mv_start, mv_end);
    if (!dst.has_value())
        return execute_result_type{};

    // find memory location that contains this memory:
    MemoryLevel* m = memory_subsystem_[memory_level_map_[ld]];
    MemoryAccessResult result = m->do_coupled_load_store(ld, st);
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

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
_num_routing_channels(ComputeSubsystem* c)
{
    const size_t total_capacity = c->local_memory_capacity + c->dedicated_ancilla_count;
    return (total_capacity + MAX_QUBITS_PER_CHANNEL - 1) / MAX_QUBITS_PER_CHANNEL;
}

size_t
_channel_width(ComputeSubsystem* c)
{
    size_t q_count = c->dedicated_ancilla_count + c->local_memory_capacity;
    if (q_count & 1)
        q_count++;
    q_count = std::min(MAX_QUBITS_PER_CHANNEL, q_count);
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
_get_idx_in_array(Qubit* q, const std::vector<Qubit*>& arr)
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

template <class LockPred> std::optional<cycle_type>
_test_multiple_time_intervals(cycle_type start, cycle_type delta, cycle_type start_max, const LockPred& pred)
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
