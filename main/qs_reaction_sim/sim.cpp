/*
 *  author: Suhas Vittal
 *  date:   10 July 2026
 * */

#include "qs_reaction_sim/sim.h"
#include "qs_reaction_sim/rad.h"

#define RS_VERBOSE

namespace rs
{

bool GL_RAD_ENABLED{false};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using routing_type = Driver::routing_type;

constexpr size_t CHANNEL_CAPACITY{32};

/*
 * `n` = program qubits. Used to set up routing object.
 * */
size_t _num_routing_channels(size_t);
size_t _channel_width(size_t);

bool _can_retire_immediately(Instruction::Type);
cycle_type _inst_latency(Instruction::Type, size_t d);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

Driver::routing_type::routing_type(size_t n)
    :sim::routing::MultiChannelBus<routing_type>(_num_routing_channels(n), _channel_width(n))
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

Driver::Driver(std::string trace_file, config_type conf)
    :sim::Operable("ReactionSim", /* does not matter: */ 1),
    decoder_traits(conf.code_distance, conf.reaction_time),
    decoder_count(conf.decoder_count),
    code_distance(conf.code_distance),
    decoder_next_available_cycle_(decoder_traits.pwd_reaction_time())
{
    // Open trace file:
    uint32_t qubit_count{};
    generic_strm_open(trace_strm_, trace_file, "rb");
    generic_strm_read(trace_strm_, &qubit_count, sizeof(uint32_t));
    program_qubits_ = qubit_count;

    // initialize `dag_` and `syndrome_history_` which depend on `program_qubits_`
    dag_ = std::make_unique<DAG>(program_qubits_);
    syndrome_history_ = std::make_unique<History>(program_qubits_, code_distance, HistoryRole::Reaction);

    program_qubit_available_cycle_.resize(program_qubits_, 0);
    anc_available_cycle_.reserve(128);

    // initialize routing space.
    // we also need to map program qubits to their location
    // within the routing space.
    routing_ = std::make_unique<routing_type>(program_qubits());
    for (qubit_type i = 0; i < program_qubits(); i++)
    {
        size_t ii{i};
        const auto ch = ii % routing_->num_channels;
        ii /= routing_->num_channels;
        const auto ro = ii % 2;
        ii /= 2;
        const auto co = ii;
        routing_->set_location(i, ch, ro, co);
    }

    // Safety: assert that the decoder's throughput is high enough:
    //  You can uncomment this if you are interested in what will happen.
    //  Believe me, one of the first things I did was make sure that
    //  insufficient throughput results in a deadlock (assuming you
    //  have a T gate somewhere in the circuit).
    const double tp = decoder_traits.pwd_throughput(decoder_count);
    if (tp < 1.0)
        std::cerr << "Driver:: decoder throughput does not meet backlog criterion: tp = " << tp << " < 1" << _die{};

    // RAD declaration
    if (GL_RAD_ENABLED)
    {
        rad_ = std::make_unique<RAD>(program_qubits_,
                                     conf.rad.decoder_count,
                                     DecoderTraits(conf.code_distance, conf.rad.reaction_time),
                                     conf.rad.fast_decoder_error_probability);
    }
}


Driver::~Driver()
{
    generic_strm_close(trace_strm_);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

long
Driver::operate()
{
    long progress{0};

    if (GL_RAD_ENABLED)
        progress += rad_->operate(current_cycle());

    // 1. Handle retired non-cliffords in front layer
    for (auto* inst : dag_->get_front_layer())
    {
        auto* uop = (inst->uop_count() > 0) ? inst->current_uop() : inst;
        if (is_t_like_instruction(uop->type) && uop->rx.retireable)
        {
            retire_instruction(inst);
            progress++;
        }
    }

    // 2. Read through front layer of DAG and execute instructions:
    fetch_into_dag();
    progress += execute_instructions_from_dag(dag_, false);
    if (GL_RAD_ENABLED)
        progress += execute_instructions_from_dag(rad_->wrong_path_dag(), true);

    // 3. handle decoding in `syndrome_history_`
    if (current_cycle() >= decoder_next_available_cycle_)
        decode_history();

    // 4. end of cycle
    add_idle_cycle_array(syndrome_history_, current_cycle(), program_qubit_available_cycle_);
    add_idle_cycle_map(syndrome_history_, current_cycle(), anc_available_cycle_);
    if (GL_RAD_ENABLED)
        rad_->add_idle_cycle(current_cycle(), program_qubit_available_cycle_);
    return progress;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
Driver::print_progress(std::ostream& ostrm) const
{
    ostrm << "t = " << current_cycle() << " ===================================================\n";

    ostrm << "DAG front layer:";
    for (auto* inst : dag_->get_front_layer())
        ostrm << "\n\t" << *inst;
    ostrm << "\nSyndrome history front layer:";

    std::vector<qubit_type> qubit_print_list(program_qubits());
    std::iota(qubit_print_list.begin(), qubit_print_list.end(), 0);
    for (const auto& [q, __unused] : anc_available_cycle_)
        qubit_print_list.push_back(q);
    for (auto q : qubit_print_list)
    {
        auto* e = syndrome_history_->front(q);
        if (e != nullptr && e->type != HistoryEvent::Idle)
        {
            ostrm << "\n\tq" << q << " : " << *e;
            for (auto* f : e->predecessors)
                ostrm << "\n\t\tpred: " << *f;
        }
    }
    ostrm << "\nIPdC = " << ipc()*code_distance 
            << "\ninst done = " << s_inst_done
            << "\nevent count = " << syndrome_history_->event_count();
    if (GL_RAD_ENABLED)
    {
        ostrm << "\nRAD event count = " << rad_->history()->event_count()
                << "\nevent count ratio = " << fpdiv(syndrome_history_->event_count(), rad_->history()->event_count());
    }
    ostrm << "\n\n";
}

void
Driver::print_deadlock_info(std::ostream& ostrm) const
{
    print_progress(ostrm);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
Driver::fetch_into_dag()
{
    constexpr size_t WATERMARK{16*1024};
    while (dag_->inst_count() < WATERMARK && !generic_strm_eof(trace_strm_))
    {
        inst_ptr inst = read_instruction_from_stream(trace_strm_);
        if (inst == nullptr || generic_strm_eof(trace_strm_))
            break;
        inst->number = s_inst_read++;
        if (is_software_instruction(inst->type))
        {
            delete inst;
            continue;
        }
        // remove any software instructions from an RZ urotseq:
        if (is_rotation_instruction(inst->type))
        {
            auto it = std::remove_if(inst->urotseq.begin(), inst->urotseq.end(),
                            [] (auto t) { return is_software_instruction(t); });
            inst->urotseq.erase(it, inst->urotseq.end());
        }
        dag_->add_instruction(inst);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
Driver::execute_instruction(inst_ptr inst)
{
    if (is_software_instruction(inst->type) || inst->type == Instruction::Type::H)
    {
        inst->first_ready_cycle = current_cycle();  // we need this line to avoid segfaults later.
        return true;
    }

    // check if operands are available:
    const bool all_available = std::all_of(inst->q_begin(), inst->q_end(),
                                    [this] (auto q) 
                                    { 
                                        const bool avail = current_cycle() >= program_qubit_available_cycle_[q]; 
                                        return avail;
                                    });
    if (!all_available)
        return false;

    // if this operation requires routing, then handle it:
    const bool routing_space_locked = handle_routing(inst);
    if (!routing_space_locked)
        return false;

    inst->first_ready_cycle = current_cycle();

    // add instruction to `syndrome_history_`
    auto result = syndrome_history_->add_events_for_instructions(inst, current_cycle());
    inst->rx.executed = true;

    // update qubit availability cycle:
    const auto avail_cycle = current_cycle() + _inst_latency(inst->type, code_distance);
    for (auto q : inst->qubits)
        program_qubit_available_cycle_[q] = avail_cycle;
    for (auto q : result.ancilla)
        anc_available_cycle_[q] = avail_cycle;

    // also add to `rad_`'s history
    if (GL_RAD_ENABLED)
        rad_->register_instruction_in_history(inst, current_cycle(), avail_cycle);

    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
Driver::decode_history()
{
    // TODO: we need to also handle limited amounts of decoders.
    // This assumes that we have `decoder_count` decoders per program
    // qubit, giving each qubit a budget of `2*decoder_count` windows.
    const size_t max_windows = 2*decoder_count;
    auto deallocated_ancilla = syndrome_history_->decode(current_cycle(), max_windows);
    for (auto a : deallocated_ancilla)
        anc_available_cycle_.erase(a);

    auto tr = decoder_traits.pwd_reaction_time();
    decoder_next_available_cycle_ = current_cycle() + tr;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
Driver::retire_instruction(inst_ptr inst)
{
    update_stats(inst->uop_count() > 0 ? inst->current_uop() : inst);

    if (GL_RAD_ENABLED)
    {
        if (inst->uop_count() > 0)
        {
            rad_->retire_instruction(inst->current_uop());
            // we do not want to delete the current uop, just advance:
            if (inst->advance_uop())
            {
                // ok to delete macro op as we have moved all of its uops
                // into `RAD`
                dag_->remove_instruction_from_front_layer(inst);
                delete inst;
            }
        }
        else
        {
            if (inst->rx.is_non_program_instruction)
                rad_->wrong_path_dag()->remove_instruction_from_front_layer(inst);
            else
                dag_->remove_instruction_from_front_layer(inst);
            rad_->retire_instruction(inst);
        }
    }
    else
    {
        const bool destroy_inst = (inst->uop_count() == 0) || inst->retire_current_uop();
        if (destroy_inst)
        {
            dag_->remove_instruction_from_front_layer(inst);
            delete inst;
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
Driver::handle_routing(inst_ptr inst)
{
    const size_t d{code_distance};

    if (is_cx_like_instruction(inst->type))
    {
        auto q0 = inst->qubits[0],
             q1 = inst->qubits[1];
        if (!routing_->test_resources_between(q0, q1, current_cycle(), current_cycle()+2*d))
            return false;
        routing_->lock_resources_between(q0, q1, current_cycle(), current_cycle()+2*d);
        // set distance between q0 and q1: 
        const size_t p = routing_->patch_distance(q0, q1) + 2;  // add two for the src/dst themselves
        inst->rx.routing_space_consumed = p;
        s_cx_routing_overhead.add(p);
    }
    else if (is_s_like_instruction(inst->type))
    {
        auto q = inst->qubits[0];
        if (!routing_->test_local_resource(q, current_cycle(), current_cycle()+d))
            return false;
        routing_->lock_local_resource(q, current_cycle(), current_cycle()+d);
    }
    else if (is_t_like_instruction(inst->type))
    {
        using namespace sim::routing;

        auto q = inst->qubits[0];
        const bool lhs = routing_->test_resources_between(q, MCB_LEFT_ENTRY, current_cycle(), current_cycle()+d),
                   rhs = routing_->test_resources_between(q, MCB_RIGHT_ENTRY, current_cycle(), current_cycle()+d);
        // select between `MCB_LEFT_ENTRY` and `MCB_RIGHT_ENTRY`: whichever is free
        if (!lhs && !rhs)
            return false;
        // compute patch distance to `MCB_LEFT_ENTRY` and `MCB_RIGHT_ENTRY`. In case of `lhs && rhs`,
        // choose smaller patch distance.
        const auto p_lhs = routing_->patch_distance(q, MCB_LEFT_ENTRY),
                    p_rhs = routing_->patch_distance(q, MCB_RIGHT_ENTRY);
        int64_t dst;
        size_t p; // patch_distance
        if (lhs && (!rhs || p_lhs < p_rhs))
        {
            dst = MCB_LEFT_ENTRY;
            p = p_lhs;
        }
        else
        {
            dst = MCB_RIGHT_ENTRY;
            p = p_rhs;
        }
        routing_->lock_resources_between(q, dst, current_cycle(), current_cycle()+d);
        p += 2;
        inst->rx.routing_space_consumed = p;
        s_t_routing_overhead.add(p);
    }
    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
Driver::update_stats(inst_ptr inst)
{
    if (inst->rx.is_non_program_instruction)
        return;
    s_inst_done++;
    auto latency = (current_cycle() - *inst->first_ready_cycle);
    if (is_t_like_instruction(inst->type))
        s_t_latency.add(latency);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

long
Driver::execute_instructions_from_dag(const dag_ptr& d, bool ignore_wrong_path_blockage)
{
    if (GL_RAD_ENABLED && rad_->is_resolving_wrong_path())
        return 0;

    long progress{0};
    for (auto* inst : d->get_front_layer())
    {
        auto* uop = (inst->uop_count() > 0) ? inst->current_uop() : inst;
        if (uop->rx.executed)
            continue;

        // if not `ignore_wrong_path_blockage`, then ensure none of the qubits are blocked
        if (GL_RAD_ENABLED && !ignore_wrong_path_blockage)
        {
            const bool any_blocked = std::any_of(inst->q_begin(), inst->q_end(),
                                        [this] (auto q) { return rad_->is_qubit_blocked_by_wrong_path(q); });
            if (any_blocked)
                continue;
        }

        const bool success = execute_instruction(uop);
        if (!success)
            continue;

        // if the instruction is a clifford, then we can retire it
        // immediately.
        if (_can_retire_immediately(uop->type))
        {
            retire_instruction(inst);
            progress++;
        }
    }
    return progress;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

size_t
_num_routing_channels(size_t n)
{
    return (n+CHANNEL_CAPACITY-1)/CHANNEL_CAPACITY;
}

size_t
_channel_width(size_t n)
{
    return CHANNEL_CAPACITY/2;
}

bool
_can_retire_immediately(Instruction::Type t)
{
    return !is_t_like_instruction(t);
}

cycle_type
_inst_latency(Instruction::Type t, size_t d)
{
    if (is_t_like_instruction(t))
        return d;
    else if (is_s_like_instruction(t))
        return d;
    else if (is_cx_like_instruction(t))
        return 2*d;
    else
        return 0;
}

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace rs
