/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#include "perf_sim/client.h"

namespace sim
{

extern bool GL_ELIDE_CLIFFORDS;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using inst_ptr = Client::inst_ptr;

bool _memoize_pred(const inst_ptr);
void _clean_urotseq(Instruction::urotseq_type&);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

Client::Client(std::string _trace_file, int8_t _id)
    :trace_file(_trace_file),
    id(_id),
    tristrm_(),
    num_qubits(open_file_and_read_qubit_count()),
    dag_{new DAG(num_qubits)},
    qubits_(num_qubits)
{
    for (qubit_type q_id = 0; q_id < num_qubits; q_id++)
        qubits_[q_id] = new Qubit{q_id, id};
}

Client::~Client()
{
    generic_strm_close(tristrm_);

    for (auto* q : qubits_)
        delete q;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
Client::warmup_dag(size_t s)
{
    while (dag_->inst_count() < s && !eof())
    {
        inst_ptr inst = read_instruction_from_trace();
        // immediately elide software instructions here
        if (is_software_instruction(inst->type))
            delete inst;
        else
            dag_->add_instruction(inst, _memoize_pred(inst));
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
Client::retire_instruction(inst_ptr inst)
{
    cycle_type inst_latency = inst->cycle_done - *inst->first_ready_cycle;

    if (is_memory_access(inst->type))
    {
        s_memory_accesses++;
        s_memory_access_latency += inst_latency;

        goto kill_instruction;
    }

    s_inst_done++;
    s_unrolled_inst_done += inst->original_unrolled_inst_count;

    if (is_t_like_instruction(inst->type))
        s_t_gates_done++;

    if (is_rotation_instruction(inst->type))
    {
        s_rotation_latency += inst_latency;
        s_total_rotation_uops += inst->original_unrolled_inst_count;
        s_total_rotations++;

        s_t_gates_done += std::count_if(inst->urotseq.begin(), inst->urotseq.end(), 
                                    [] (auto t) { return is_t_like_instruction(t); });
    }

kill_instruction:
    dag_->remove_instruction_from_front_layer(inst);
    delete inst;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
Client::eof() const
{
    return generic_strm_eof(tristrm_);
}

const std::unique_ptr<DAG>&
Client::dag() const
{
    return dag_;
}

const std::vector<Qubit*>&
Client::qubits() const
{
    return qubits_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
Client::open_file_and_read_qubit_count()
{
    uint32_t n;
    generic_strm_open(tristrm_, trace_file, "rb");
    generic_strm_read(tristrm_, &n, 4);
    return static_cast<size_t>(n);
}

inst_ptr
Client::read_instruction_from_trace()
{
    inst_ptr inst = read_instruction_from_stream(tristrm_);
    
    if (inst == nullptr)
    {
        std::cerr << "Client::read_instruction_from_file: client " << static_cast<int>(id)
                << " hit eof for trace \"" << trace_file << "\"" << _die{};
    }

    inst->number = s_inst_read++;

    if (GL_ELIDE_CLIFFORDS && !is_rotation_instruction(inst->type) && !is_memory_access(inst->type))
    {
        delete inst;
        return read_instruction_from_trace();
    }

    // go through and remove all software instructions from the `urotseq` if this is a
    // rotation instruction:
    _clean_urotseq(inst->urotseq);
    for (auto& u : inst->corr_urotseq_array)
        _clean_urotseq(u);

    inst->original_unrolled_inst_count = inst->unrolled_inst_count();

    return inst;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

bool
_memoize_pred(const inst_ptr inst)
{
    return is_rotation_instruction(inst->type);
}

void
_clean_urotseq(Instruction::urotseq_type& u)
{
    auto it = std::remove_if(u.begin(), u.end(), 
            [] (auto t) { return is_software_instruction(t) || (GL_ELIDE_CLIFFORDS && !is_t_like_instruction(t)); });
    u.erase(it, u.end());
}

}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim
