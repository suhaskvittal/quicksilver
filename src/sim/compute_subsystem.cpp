/*
 *  contributor: OpenAI Codex (GPT-6.1 Sol), TopoLS integration, 1 October 2026
 *  author: Suhas Vittal
 *  date:   11 March 2026
 *  contributor: OpenAI Codex (routing and EDPC integration; TopoLS model removal, 30 September 2026)
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
using routing_xtor = ComputeSubsystem::routing_translator;
using routing_ptr = ComputeSubsystem::routing_ptr;
using mcb_type = routing::MultiChannelBus<routing_xtor>;
using edpc_type = routing::EDPCLayout<routing_xtor>;

size_t _dedicated_ancilla_count();

template <class ArrayLike> 
void _fill_dedicated_ancilla(ArrayLike&, size_t&, size_t, client_id_type);

/*
 * Routing space initializers. These functions should map all dedicated ancilla and program
 * qubits into the routing space. They should return a raw pointer allocated on the heap.
 * The caller should manage this pointer however they want (i.e., wrap it in a `std::unique_ptr`).
 * */
mcb_type* _build_multi_channel_bus(ComputeSubsystem*);
edpc_type* _build_edpc_layout(ComputeSubsystem*);

/*
 * `_read_address_and_shift` does `x % resource_size` and then modifies `x`: `x /= resource_size`.
 * This is useful for mapping qubits into the routing space.
 * */
int _read_address_and_shift(int&, size_t resource_size);

/*
 * Calls `test_resources_between()` for both of `routing::MCB_LEFT_ENTRY` and
 * `routing::MCB_RIGHT_ENTRY`, and returns the first one that can be locked. 
 * Return std::nullopt if neither are available.
 * */
template <class Src>
std::optional<routing::id_type> _test_endpoints_and_return_first_lockable(routing_ptr&,
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
cycle_type _get_earliest_lockable_time_between(ComputeSubsystem*,
                                                Src, 
                                                Dst, 
                                                cycle_type current_cycle, 
                                                cycle_type lock_duration);

template <class Src>
cycle_type _get_earliest_lockable_time_for_endpoints(ComputeSubsystem*,
                                                        Src, 
                                                        cycle_type current_cycle, 
                                                        cycle_type lock_duration);

/*
 * Other functions:
 * */
size_t _get_idx_in_array(Qubit*, const std::vector<Qubit*>&);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ComputeSubsystem::routing_translator::routing_translator(ComputeSubsystem* _c)
    :c(_c)
{}

routing::id_type
ComputeSubsystem::routing_translator::translate(Qubit* q) const
{
    // Both layouts use the same IDs: dedicated ancilla first, then program
    // qubits. Each layout maps those IDs to its own physical coordinates.
    if (q->client_id == RDR_CLIENT_ID || q->client_id == TOPOLS_CLIENT_ID)
        return _get_idx_in_array(q, c->dedicated_ancilla());
    else
        return c->dedicated_ancilla_count + _get_idx_in_array(q, c->local_memory());
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ComputeSubsystem::ComputeSubsystem(double freq_khz,
                                      size_t _code_distance,
                                      size_t _local_memory_capacity,
                                      BulkProducer* t_producer,
                                      memory_subsystem_type memory_subsystem,
                                      RoutingLayout _routing_layout)
    :Operable("compute_subsystem", freq_khz),
    code_distance(_code_distance),
    local_memory_capacity(_local_memory_capacity),
    dedicated_ancilla_count(_dedicated_ancilla_count()),
    routing_layout(_routing_layout),
    local_memory_(local_memory_capacity),
    t_producer_(t_producer),
    memory_subsystem_(memory_subsystem),
    dedicated_ancilla_(dedicated_ancilla_count)
{
    if (routing_layout == RoutingLayout::BUS)
        routing_ = routing_ptr{ _build_multi_channel_bus(this) };
    else
        routing_ = routing_ptr{ _build_edpc_layout(this) };

    // we can initialize the dedicated qubits now:
    if (dedicated_ancilla_count > 0)
    {
        // first handle RDR handle ancilla (if enabled)
        size_t idx{0};
        if (GL_RDR_ENABLED)
            _fill_dedicated_ancilla(dedicated_ancilla_, idx, GL_RDR_CAPACITY, RDR_CLIENT_ID);
        if (GL_TOPOLS_ENABLED)
            _fill_dedicated_ancilla(dedicated_ancilla_, idx, GL_TOPOLS_ANCILLA, TOPOLS_CLIENT_ID);
        if (idx < dedicated_ancilla_count)
            std::cerr << "ComputeSubsystem::ctor(): did not fill up all requested dedicated ancilla" << _die{};
    }
}

ComputeSubsystem::~ComputeSubsystem()
{
    for (auto* q : dedicated_ancilla_)
        delete q;
}

size_t
ComputeSubsystem::edpc_patch_count() const
{
    assert(routing_layout == RoutingLayout::EDPC);
    return static_cast<const edpc_type*>(routing_.get())->patch_count();
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
ComputeSubsystem::execute_instruction(inst_ptr inst, const std::vector<Qubit*>& args)
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
    routing_->lock_resources_between(q, *channel_out, tp_start, tp_end);

    // update the result latency which is incorrect
    result.latency = 3*code_distance + GL_REACTION_TIME;  // ZZ with |T> + teleporting XX and ZZ + feedforward
    while (inst->uops_retired() < inst->uop_count() && remaining > 0)
    {
        auto* uop = inst->current_uop();
        // operation occurs on an ancilla: assume operation always succeeds
        if (is_t_like_instruction(uop->type))
        {
            if (!t_producer_->consume(1))
                break;
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
    const cycle_type c = _get_earliest_lockable_time_for_endpoints(this, q, current_cycle(), d);
    auto dst = _test_endpoints_and_return_first_lockable(routing_, q, c, c+d);
    if (!dst.has_value())
        return false;
    // lock routing space and return true:
    routing_->lock_resources_between(q, *dst, c, c+d);
    q->cycle_available = c+d+1;  // +1 due to destruction by X measurement
    return true;
}

bool
ComputeSubsystem::rdr_apply_rotation_magic_state_from_surface_code(Qubit* q, Qubit* m)
{
    const size_t d = code_distance;
    const cycle_type c = _get_earliest_lockable_time_between(this, q, m, current_cycle(), d);
    if (!routing_->test_resources_between(q, m, c, c+d))
        return false;
    routing_->lock_resources_between(q, m, c, c+d);
    q->cycle_available = c + d + GL_REACTION_TIME;
    m->cycle_available = c + d + 1;
    return true;
}

bool
ComputeSubsystem::rdr_apply_rotation_magic_state_from_memory(Qubit* q)
{
    const size_t d = code_distance;
    const cycle_type c = _get_earliest_lockable_time_for_endpoints(this, q, current_cycle(), d);
    auto dst = _test_endpoints_and_return_first_lockable(routing_, q, c, c+d);
    if (!dst.has_value())
        return false;
    // lock routing space and return true:
    routing_->lock_resources_between(q, *dst, c, c+d);
    q->cycle_available = c + d + GL_REACTION_TIME;
    return true;
}

double
ComputeSubsystem::log_fidelity(Client* c, double scale, double d_freq_khz, double p) const
{
    const double cycles = convert_cycles_between_frequencies(c->s_cycle_complete, d_freq_khz, freq_khz) * scale;
    const double ler_per_cycle = configuration::surface_code_logical_error_rate(code_distance, p);
    const double t_gates = c->s_t_gates_done * scale;
    const double t_infidelity = t_producer_->top_level_infidelity();

    // memory (idle) fidelity
    const double log_f_mem = (local_memory_capacity + (GL_TOPOLS_ENABLED ? dedicated_ancilla_count : 0)) * cycles * std::log(1 - ler_per_cycle);
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
    return t_producer_->top_level_buffer_occupancy();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
ComputeSubsystem::epr_credit_available_for(Qubit* ld, Instruction::Type t) const
{
    auto it = memory_level_map_.find(ld);
    if (it == memory_level_map_.end() || it->second < 0)
        return true;   // not currently resident in remote memory
    return memory_subsystem_[it->second]->has_epr_credit(ld, t);
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
    if (routing_->test_local_resource(q, current_cycle(), current_cycle()+code_distance))
    {
        routing_->lock_local_resource(q, current_cycle(), current_cycle()+code_distance);
        return execute_result_type{.progress=1, .latency=code_distance};
    }
    return execute_result_type{};
}

execute_result_type
ComputeSubsystem::do_cx_like_gate(inst_ptr inst, Qubit* c, Qubit* t)
{
    if (routing_->test_resources_between(c, t, current_cycle(), current_cycle() + 2*code_distance))
    {
        routing_->lock_resources_between(c, t, current_cycle(), current_cycle() + 2*code_distance);
        return execute_result_type{.progress=1, .latency=2*code_distance};
    }
    return execute_result_type{};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

execute_result_type
ComputeSubsystem::do_t_like_gate(inst_ptr inst, Qubit* q)
{
    // first make sure a magic state is available:
    if (t_producer_->top_level_buffer_occupancy() == 0)
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
    routing_->lock_resources_between(q, *dst, t_start, t_end);

    // estimate decoding latency:
    const cycle_type total_latency = code_distance + GL_REACTION_TIME;
    t_producer_->consume(1);
    return execute_result_type{.progress=1, .latency=total_latency};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

cycle_type
ComputeSubsystem::next_memory_access_cycle(Qubit* ld, Instruction::Type t) const
{
    const auto level = memory_level_map_.at(ld);
    if (level < 0)
        return current_cycle();
    const auto* memory = memory_subsystem_[level];
    const auto ready = memory->next_ready_cycle_for_access(ld, t);
    if (ready <= memory->current_cycle())
        return current_cycle();
    // current_cycle() is the next cycle to execute. The adapter becomes usable
    // after the preceding memory tick advances that counter to `ready`.
    return std::max(current_cycle()+1, convert_cycles_between_frequencies(ready-1,
                                             memory->freq_khz, freq_khz)+1);
}

execute_result_type
ComputeSubsystem::do_memory_access(inst_ptr inst, const std::vector<Qubit*>& args)
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
    if (!result.success())
        return execute_result_type{};

    // update data structures
    routing_->lock_resources_between(st, *dst, mv_start, mv_end);
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
_dedicated_ancilla_count()
{
    size_t n{0};
    if (GL_TOPOLS_ENABLED)
        n += GL_TOPOLS_ANCILLA;
    if (GL_RDR_ENABLED)
        n += GL_RDR_CAPACITY;
    return n;
}

template <class ArrayLike> void
_fill_dedicated_ancilla(ArrayLike& a, size_t& idx, size_t n, client_id_type c)
{
    for (size_t i = 0; i < n; i++)
        a[idx++] = new Qubit{ .qubit_id=static_cast<qubit_type>(idx), .client_id=c };
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

mcb_type*
_build_multi_channel_bus(ComputeSubsystem* c)
{
    constexpr size_t CHANNEL_CAP{8};

    size_t num_channels,
           channel_width;
    const size_t n_prog = c->local_memory_capacity;
    const size_t n_tot = n_prog + c->dedicated_ancilla_count;
    num_channels = (n_tot + CHANNEL_CAP - 1) / CHANNEL_CAP;
    channel_width = std::min(CHANNEL_CAP, n_tot+1) >> 1;

    // initialize contents of `r`
    auto* r = new mcb_type(num_channels, channel_width, routing_xtor{c});
    for (size_t i = 0; i < n_tot; i++)
    {
        int ii = static_cast<int>(i);
        int ch = _read_address_and_shift(ii, num_channels);
        int ro = _read_address_and_shift(ii, 2);
        // only thing remaining should be column address: assert that it is bounded by `channel_width`
        if (ii >= channel_width)
        {
            std::cerr << "_build_multi_channel_bus: invalid column address when mapping data, qubit id = "
                        << i << ", co = " << ii << ", channel width = " << channel_width << _die{};
        }
        int co = ii;
        r->set_location(i, ch, ro, co);
    }
    return r;
}

edpc_type*
_build_edpc_layout(ComputeSubsystem* c)
{
    const size_t patch_count = c->local_memory_capacity + c->dedicated_ancilla_count;
    auto* layout = new edpc_type(patch_count, routing_xtor{c});
    for (size_t i = 0; i < patch_count; i++)
        layout->set_location(i, i);
    return layout;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
_read_address_and_shift(int& x, size_t n)
{
    int y = x % static_cast<int>(n);
    x /= static_cast<int>(n);
    return y;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T> std::optional<routing::id_type>
_test_endpoints_and_return_first_lockable(routing_ptr& r, T src, cycle_type from, cycle_type to)
{
    for (routing::id_type dst : {routing::MCB_LEFT_ENTRY, routing::MCB_RIGHT_ENTRY})
        if (r->test_resources_between(src, dst, from, to))
            return std::make_optional(dst);
    return std::nullopt;
}

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

template <class T, class U> cycle_type
_get_earliest_lockable_time_between(ComputeSubsystem* compute, T src, U dst, cycle_type c, cycle_type d)
{
    cycle_type out{0};
    compute->for_each_resource_between(src, dst, c, c+d,
            [&out, c, d] (const auto& res)
            {
                out = std::max(out, res.next_ready_cycle(c, d));
            });
    return out;
}

template <class T> cycle_type
_get_earliest_lockable_time_for_endpoints(ComputeSubsystem* compute, T src, cycle_type c, cycle_type d)
{
    cycle_type earliest{std::numeric_limits<cycle_type>::max()};
    for (routing::id_type dst : {routing::MCB_LEFT_ENTRY, routing::MCB_RIGHT_ENTRY})
        earliest = std::min(earliest, _get_earliest_lockable_time_between(compute, src, dst, c, d));
    return earliest;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
_get_idx_in_array(Qubit* q, const std::vector<Qubit*>& arr)
{
    auto it = std::find(arr.begin(), arr.end(), q);
    assert(it != arr.end());
    return std::distance(arr.begin(), it);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim
