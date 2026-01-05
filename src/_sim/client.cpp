/*
    author: Suhas Vittal
    date:   15 September 2025
*/

#include "client.h"

#include <iostream>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
QUBIT::operator==(const QUBIT& other) const
{
    return client_id == other.client_id && qubit_id == other.qubit_id;
}

std::string
QUBIT::to_string() const
{
    return "q" + std::to_string(qubit_id) + " (c" + std::to_string(client_id) + ")";
}

std::ostream&
operator<<(std::ostream& os, const QUBIT& q)
{
    os << q.to_string();
    return os;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

CLIENT::CLIENT(std::string trace_file, int8_t id)
    :trace_file(trace_file),
    id(id),
    num_qubits(open_file())
{}

CLIENT::~CLIENT()
{
    close_file();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

CLIENT::inst_ptr
CLIENT::read_instruction_from_trace()
{
    INSTRUCTION::io_encoding enc{};

    if (eof())
    {
        has_hit_eof_once = true;
        // reload the file:
        close_file();
        open_file();
    }

    enc.read_write([this] (void* buf, size_t size) { return generic_strm_read(this->trace_istrm, buf, size); });

    inst_ptr inst{new INSTRUCTION(std::move(enc))};
    inst->inst_number = s_inst_read++;
    return inst;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
CLIENT::eof() const
{
    return generic_strm_eof(trace_istrm);
}

size_t
CLIENT::open_file()
{
    uint32_t n{0};
    generic_strm_open(trace_istrm, trace_file, "rb");
    generic_strm_read(trace_istrm, &n, 4);
    return static_cast<size_t>(n);
}

void
CLIENT::close_file()
{
    generic_strm_close(trace_istrm);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim