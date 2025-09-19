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

    if (trace_istrm.index() == GEN_IO_BIN_IDX)
    {
        FILE* istrm = std::get<FILE*>(trace_istrm);
        enc.read_write([istrm] (void* buf, size_t size) { return fread(buf, 1, size, istrm); });
    }
    else
    {
        gzFile istrm = std::get<gzFile>(trace_istrm);
        enc.read_write([istrm] (void* buf, size_t size) { return gzread(istrm, buf, size); });
    }

    inst_ptr inst{new INSTRUCTION(std::move(enc))};
    inst->inst_number = s_inst_read++;
    return inst;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
CLIENT::eof() const
{
    if (trace_istrm.index() == GEN_IO_BIN_IDX)
        return feof(std::get<FILE*>(trace_istrm));
    else
        return gzeof(std::get<gzFile>(trace_istrm));
}

size_t
CLIENT::open_file()
{
    bool is_gz = trace_file.find(".gz") != std::string::npos;
    uint32_t n{0};
    if (is_gz)
    {
        trace_istrm = gzopen(trace_file.c_str(), "rb");
        gzread(std::get<gzFile>(trace_istrm), &n, 4);
    }
    else
    {
        trace_istrm = fopen(trace_file.c_str(), "rb");
        fread(&n, 4, 1, std::get<FILE*>(trace_istrm));
    }
    return static_cast<size_t>(n);
}

void
CLIENT::close_file()
{
    if (trace_istrm.index() == GEN_IO_BIN_IDX)
        fclose(std::get<FILE*>(trace_istrm));
    else
        gzclose(std::get<gzFile>(trace_istrm));
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim