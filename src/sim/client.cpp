/*
    author: Suhas Vittal
    date:   25 August 2025
*/

#include "client.h"

#include <fstream>
#include <iostream>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

CLIENT::CLIENT(std::string _trace_file, int8_t _id)
    :trace_file(_trace_file),
    id(_id)
{
    size_t num_qubits = open_file(trace_file);

    qubits = std::vector<qubit_info_type>(num_qubits);
    for (size_t i = 0; i < qubits.size(); i++)
    {
        qubits[i].memloc_info.client_id = id;
        qubits[i].memloc_info.qubit_id = i;
    }
}

CLIENT::~CLIENT()
{
    if (trace_file_type == TRACE_FILE_TYPE::GZ)
        gzclose(trace_gz_istrm);
    else
        fclose(trace_bin_istrm);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

INSTRUCTION
CLIENT::read_instruction_from_trace()
{
    INSTRUCTION::io_encoding enc{};
    
    // reopen the file if we hit EOF
    if (eof())
    {
        if (stop_at_eof)
            return INSTRUCTION{};

        if (trace_file_type == TRACE_FILE_TYPE::GZ)
            gzclose(trace_gz_istrm);
        else
            fclose(trace_bin_istrm);

        open_file(trace_file);
    }

    if (trace_file_type == TRACE_FILE_TYPE::GZ)
        enc.read_write([this] (void* buf, size_t size) { return gzread(this->trace_gz_istrm, buf, size); });
    else
        enc.read_write([this] (void* buf, size_t size) { return fread(buf, 1, size, this->trace_bin_istrm); });

    if (enc.type_id == static_cast<uint8_t>(INSTRUCTION::TYPE::RZ) ||
        enc.type_id == static_cast<uint8_t>(INSTRUCTION::TYPE::RX))
    {
        if (enc.urotseq_size == 0)
            throw std::runtime_error("RZ/RX instruction has empty unrolled rotation sequence");
    }

    INSTRUCTION out(std::move(enc));
    return out;
}

bool
CLIENT::eof() const
{
    return trace_file_type == TRACE_FILE_TYPE::GZ ? gzeof(trace_gz_istrm) : feof(trace_bin_istrm);
}

size_t
CLIENT::open_file(const std::string& trace_file)
{
    uint32_t num_qubits;
    if (trace_file.find(".gz") != std::string::npos)
    {
        trace_file_type = TRACE_FILE_TYPE::GZ;
        trace_gz_istrm = gzopen(trace_file.c_str(), "rb");

        gzread(trace_gz_istrm, &num_qubits, 4);
    }
    else
    {
        trace_file_type = TRACE_FILE_TYPE::BINARY;
        trace_bin_istrm = fopen(trace_file.c_str(), "rb");

        fread(&num_qubits, 4, 1, trace_bin_istrm);
    }
    return num_qubits;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim