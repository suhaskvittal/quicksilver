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
    num_qubits(open_file_and_read_qubit_count()),
    dag{new DAG(num_qubits)}
{}

CLIENT::~CLIENT()
{
    generic_strm_close(tristrm_)
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<inst_ptr>
CLIENT::get_ready_instructions()
{
    constexpr size_t DAG_WATERMARK = 16384;
    // fill up the DAG if it is below some count:
    while (dag_->inst_count() < DAG_WATERMARK)
        read_instruction_from_trace();

    return dag_->get_front_layer();
}

void
CLIENT::retire_instruction(inst_ptr inst)
{
    dag_->remove_instruction_from_front_layer(inst);
    s_inst_done++;
    s_unrolled_inst_done += std::max(size_t{1}, inst->num_uops);
}

bool
CLIENT::eof() const
{
    return has_hit_eof_once_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
CLIENT::open_file_and_read_qubit_count()
{
    uint32_t n;
    generic_strm_open(tristrm_, trace_file, "r");
    generic_strm_read(tristrm_, &n, 4);
    return static_cast<size_t>(n);
}

CLIENT::inst_ptr
CLIENT::read_instruction_from_file()
{
    if (eof())
    {
        std::cerr << "CLIENT::read_instruction_from_file: client " << static_cast<int>(id)
                << " hit eof for trace \"" << trace_file << "\"" << _die{};
    }

    INSTRUCTION::io_encoding enc{};
    enc.read_write([this] (void* buf, size_t size) { return generic_strm_read(this->tristrm_, buf, size); });
    inst_ptr inst{new INSTRUCTION(std::move(enc))};
    inst->inst_number = s_inst_read++;
    return inst;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim
