/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#include "sim/client.h"

namespace sim
{

extern bool GL_ELIDE_CLIFFORDS;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

void _clean_urotseq(INSTRUCTION::urotseq_type&);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

CLIENT::CLIENT(std::string _trace_file, int8_t _id)
    :trace_file(_trace_file),
    id(_id),
    tristrm_(),
    num_qubits(open_file_and_read_qubit_count()),
    dag_{new DAG(num_qubits)},
    qubits_(num_qubits)
{
    for (qubit_type q_id = 0; q_id < num_qubits; q_id++)
        qubits_[q_id] = new QUBIT{q_id, id};
}

CLIENT::~CLIENT()
{
    generic_strm_close(tristrm_);

    for (auto* q : qubits_)
        delete q;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
CLIENT::retire_instruction(inst_ptr inst)
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

        s_t_gates_done += std::count_if(inst->urotseq.begin(), inst->urotseq.end(), 
                                    [] (auto t) { return is_t_like_instruction(t); });
    }

kill_instruction:
    dag_->remove_instruction_from_front_layer(inst);
    delete inst;
}

bool
CLIENT::eof() const
{
    return generic_strm_eof(tristrm_);
}

const std::unique_ptr<DAG>&
CLIENT::dag() const
{
    return dag_;
}

const std::vector<QUBIT*>&
CLIENT::qubits() const
{
    return qubits_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
CLIENT::open_file_and_read_qubit_count()
{
    uint32_t n;
    generic_strm_open(tristrm_, trace_file, "rb");
    generic_strm_read(tristrm_, &n, 4);
    return static_cast<size_t>(n);
}

CLIENT::inst_ptr
CLIENT::read_instruction_from_trace()
{
    if (eof())
    {
        std::cerr << "CLIENT::read_instruction_from_file: client " << static_cast<int>(id)
                << " hit eof for trace \"" << trace_file << "\"" << _die{};
    }

    inst_ptr inst = read_instruction_from_stream(tristrm_);
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

void
_clean_urotseq(INSTRUCTION::urotseq_type& u)
{
    auto it = std::remove_if(u.begin(), u.end(), 
            [] (auto t) { return is_software_instruction(t) || (GL_ELIDE_CLIFFORDS && !is_t_like_instruction(t)); });
    u.erase(it, u.end());
}

}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim
