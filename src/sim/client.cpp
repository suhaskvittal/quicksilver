/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#include "sim/client.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

CLIENT::CLIENT(std::string _trace_file, int8_t _id)
    :trace_file(_trace_file),
    id(_id),
    tristrm_(),
    num_qubits(open_file_and_read_qubit_count()),
    dag_{new DAG(num_qubits)}
{}

CLIENT::~CLIENT()
{
    generic_strm_close(tristrm_);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
CLIENT::retire_instruction(inst_ptr inst)
{
    s_inst_done++;
    s_unrolled_inst_done += inst->unrolled_inst_count();
    dag_->remove_instruction_from_front_layer(inst);
    delete inst;
}

bool
CLIENT::eof() const
{
    return generic_strm_eof(tristrm_);
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
    return inst;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim
