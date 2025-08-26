/*
    author: Suhas Vittal
    date:   25 August 2025
*/

#include "client.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

CLIENT::CLIENT(std::string _trace_file)
    :trace_file(_trace_file)
{
    if (trace_file.find(".gz") != std::string::npos)
        trace_file_type = TRACE_FILE_TYPE::GZ;
    else
        trace_file_type = TRACE_FILE_TYPE::BINARY;
}

CLIENT::~CLIENT()
{
    if (trace_file_type == TRACE_FILE_TYPE::GZ)
        gzclose(std::get<gzFile>(trace_istrm));
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

INSTRUCTION
CLIENT::read_instruction_from_trace()
{
    INSTRUCTION::io_encoding enc;

    if (trace_file_type == TRACE_FILE_TYPE::GZ)
    {
        auto& strm = std::get<gzFile>(trace_istrm);
        enc.read_write([&strm] (void* buf, size_t size) { return gzread(strm, buf, size); });
    }
    else
    {
        auto& strm = std::get<std::ifstream>(trace_istrm);
        enc.read_write([&strm] (void* buf, size_t size) { return strm.read(static_cast<char*>(buf), size); });
    }

    return INSTRUCTION(enc);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim