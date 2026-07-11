/*
 *  author: Suhas Vittal
 *  date:   10 July 2026
 * */

#include "qs_reaction_sim/sim.h"

namespace rs
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

bool _can_retire_immediately(Instruction::Type);
cycle_type _inst_latency(Instruction::Type, size_t d);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

Driver::Driver(std::string trace_file, 
                DecoderTraits dec, 
                DecodingMethod m,
                size_t _decoder_count,
                size_t _code_distance)
    :dec_traits(dec),
    dec_method(m),
    decoder_count(_decoder_count),
    code_distance(_code_distance)
{
    uint32_t qubit_count{};
    generic_strm_open(trace_strm_, trace_file, "rb");
    generic_strm_read(trace_strm_, &qubit_count, sizeof(uint32_t));
    program_qubits_ = qubit_count;

    dag_ = std::make_unique<DAG>(program_qubits_);
    syndrome_history_ = std::make_unique<History>(program_qubits_, code_distance, m);

    program_qubit_available_cycle_.resize(program_qubits_, 0);
    anc_available_cycle_.reserve(128);
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
    for (auto* inst : dag_->get_front_layer())
    {
        auto* uop = (inst->uop_count() > 0) ? inst->current_uop() : inst;
        if (uop->rx.executed)
            continue;

        // check if operands are available:
        const bool all_available = std::all_of(uop->qubits.begin(), uop->qubits.end(),
                                        [this] (auto q) { return current_cycle() >= program_qubit_available_cycle_[q]; });
        if (!all_available)
            continue;

        // 3. add instruction to `syndrome_history_`
        auto result = syndrome_history_->add_events_for_instructions(uop);
        uop->rx.executed = true;
        // 3a. update qubit availability cycle:
        const auto avail_cycle = current_cycle() + _inst_latency(uop->type, code_distance);
        for (auto q : uop->qubits)
            program_qubit_available_cycle_[q] = avail_cycle;
        for (auto q : result.ancilla)
            anc_available_cycle_[q] = avail_cycle;

        // 4. if the instruction is a clifford, then we can retire it
        // immediately.
        if (_can_retire_immediately(uop->type))
        {
            retire_instruction(inst);
            progress++;
        }
    }

    // 5. handle decoding in `syndrome_history_`
    if (current_cycle() >= decoder_avail_next_cycle_)
    {
        // TODO: we need to also handle limited amounts of decoders.
        // This assumes that we have `decoder_count` decoders per program
        // qubit.
        std::unordered_set<HistoryEvent*> visited;  // avoid double counting
        visited.reserve(program_qubits());
        for (qubit_type q = 0; q < syndrome_history_->max_qubits; q++)
        {
            // amount of windows decoded depends on `decoder_count` = 2*decoder_count,
            const size_t max_windows = 2*decoder_count; 
            size_t windows_decoded{0};
            while (windows_decoded < max_windows)
            {
                if (syndrome_history_->front(q) == nullptr)
                    break;
                auto* e = syndrome_history_->front(q);
                if (visited.count(e) > 0)
                    break;
                visited.insert(e);

                const auto volume_remaining = e->spacetime_volume() - e->volume_decoded;
                const auto volume_to_decode = std::min(volume_remaining, max_windows - windows_decoded);

                // if there is no more windows to decode, then this can only be a conditional
                // basis measurement or its dependent correction.
                if (volume_to_decode == 0)
                    break;

                e->volume_decoded += volume_to_decode;
                if (e->is_fully_decoded())
                {
                    if (e->type == HistoryEvent::CondBasisMeas)
                        anc_available_cycle_.erase(q);
                    syndrome_history_->retire_front_event(q);
                }
                windows_decoded += volume_to_decode;
                progress += volume_to_decode;
            }
        }
        auto tr = dec_traits.pwd_reaction_time();
        decoder_avail_next_cycle_ = current_cycle() + tr;
    }

    // 6. end of cycle
    for (qubit_type q = 0; q < program_qubits_; q++)
        if (current_cycle() >= program_qubit_available_cycle_[q])
            syndrome_history_->add_idle(q, 1);
    for (auto [q, c] : anc_available_cycle_)
        if (current_cycle() >= c)
            syndrome_history_->add_idle(q, 1);
    current_cycle_++;

    return progress;
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

void
Driver::retire_instruction(inst_ptr inst)
{
    s_inst_done++;
    const bool destroy_inst = (inst->uop_count() == 0) || inst->retire_current_uop();
    if (destroy_inst)
    {
        dag_->remove_instruction_from_front_layer(inst);
        delete inst;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

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
