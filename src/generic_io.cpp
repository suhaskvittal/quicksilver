/*
    author: Suhas Vittal
    date:   23 September 2025
*/

#include "generic_io.h"

#include <limits>
#include <stdexcept>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

LZMA_FILE::LZMA_FILE(FILE* _file_istrm)
    :file_istrm(_file_istrm)
{
    lzma_strm = LZMA_STREAM_INIT;
    lzma_ret r =lzma_stream_decoder(&lzma_strm, std::numeric_limits<uint64_t>::max(), LZMA_CONCATENATED);
    if (r != LZMA_OK)
        throw std::runtime_error("lzma_stream_decoder failed: error code " + std::to_string(r));
    read_chunk_from_file();
}

LZMA_FILE::~LZMA_FILE()
{
    if (is_open)
        close();
}

size_t
LZMA_FILE::read(void* buf, size_t size)
{
    lzma_strm.next_out = (uint8_t*)buf;
    lzma_strm.avail_out = size;

    while (lzma_strm.avail_out > 0 && !eof())
    {
        if (lzma_strm.avail_in == 0 && !feof(file_istrm))
            read_chunk_from_file();
        lzma_ret r = lzma_code(&lzma_strm, feof(file_istrm) ? LZMA_FINISH : LZMA_RUN);
        if (r != LZMA_OK)
        {
            if (r == LZMA_STREAM_END)
                break;
            else
                throw std::runtime_error("lzma_code failed: error code " + std::to_string(r));
        }
    }
    
    return size - lzma_strm.avail_out;
}

bool
LZMA_FILE::eof() const
{
    return lzma_strm.avail_in == 0 && feof(file_istrm);
}

void
LZMA_FILE::close()
{
    fclose(file_istrm);
    lzma_end(&lzma_strm);
    is_open = false;
}

void
LZMA_FILE::read_chunk_from_file()
{
    size_t bytes_read = fread(lzma_buf, 1, LZMA_BUF_SIZE, file_istrm);
    lzma_strm.next_in = (uint8_t*)lzma_buf;
    lzma_strm.avail_in = bytes_read;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
generic_strm_open(generic_strm_type& strm, std::string file_path, std::string mode)
{
    if (file_path.find(".gz") != std::string::npos)
    {
        strm = gzopen(file_path.c_str(), mode.c_str());
    }
    else if (file_path.find(".xz") != std::string::npos)
    {
        FILE* file_istrm = fopen(file_path.c_str(), mode.c_str());
        strm = new LZMA_FILE(file_istrm);
    }
    else
    {
        strm = fopen(file_path.c_str(), mode.c_str());
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
generic_strm_read(generic_strm_type& strm, void* buf, size_t size)
{
    if (strm.index() == static_cast<int>(GENERIC_STRM_TYPE_ID::FILE))
        return fread(buf, 1, size, std::get<FILE*>(strm));
    else if (strm.index() == static_cast<int>(GENERIC_STRM_TYPE_ID::GZ))
        return gzread(std::get<gzFile>(strm), buf, size);
    else if (strm.index() == static_cast<int>(GENERIC_STRM_TYPE_ID::XZ))
        return std::get<LZMA_FILE*>(strm)->read(buf, size);
    else
        throw std::runtime_error("generic_strm_eof: invalid stream type: " + std::to_string(strm.index()));
}

void
generic_strm_write(generic_strm_type& strm, void* buf, size_t size)
{
    if (strm.index() == static_cast<int>(GENERIC_STRM_TYPE_ID::FILE))
        fwrite(buf, 1, size, std::get<FILE*>(strm));
    else if (strm.index() == static_cast<int>(GENERIC_STRM_TYPE_ID::GZ))
        gzwrite(std::get<gzFile>(strm), buf, size);
    else if (strm.index() == static_cast<int>(GENERIC_STRM_TYPE_ID::XZ))
        throw std::runtime_error("writing to LZMA file is not supported");
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
generic_strm_close(generic_strm_type& strm)
{
    if (strm.index() == static_cast<int>(GENERIC_STRM_TYPE_ID::FILE))
        fclose(std::get<FILE*>(strm));
    else if (strm.index() == static_cast<int>(GENERIC_STRM_TYPE_ID::GZ))
        gzclose(std::get<gzFile>(strm));
    else if (strm.index() == static_cast<int>(GENERIC_STRM_TYPE_ID::XZ))
        std::get<LZMA_FILE*>(strm)->close();
}

bool
generic_strm_eof(const generic_strm_type& strm)
{
    if (strm.index() == static_cast<int>(GENERIC_STRM_TYPE_ID::FILE))
        return feof(std::get<FILE*>(strm));
    else if (strm.index() == static_cast<int>(GENERIC_STRM_TYPE_ID::GZ))
        return gzeof(std::get<gzFile>(strm));
    else if (strm.index() == static_cast<int>(GENERIC_STRM_TYPE_ID::XZ))
        return std::get<LZMA_FILE*>(strm)->eof();
    else
        throw std::runtime_error("generic_strm_eof: invalid stream type: " + std::to_string(strm.index()));
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
generic_strm_seek(generic_strm_type& strm, size_t offset, int whence)
{
    if (strm.index() == static_cast<int>(GENERIC_STRM_TYPE_ID::FILE))
        fseek(std::get<FILE*>(strm), offset, whence);
    else if (strm.index() == static_cast<int>(GENERIC_STRM_TYPE_ID::GZ))
        gzseek(std::get<gzFile>(strm), offset, whence);
    else if (strm.index() == static_cast<int>(GENERIC_STRM_TYPE_ID::XZ))
        throw std::runtime_error("seeking in LZMA file is not supported");
    else
        throw std::runtime_error("generic_strm_eof: invalid stream type: " + std::to_string(strm.index()));
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
generic_strm_is_for_compressed_file(const generic_strm_type& strm)
{
    return strm.index() == static_cast<int>(GENERIC_STRM_TYPE_ID::GZ) || strm.index() == static_cast<int>(GENERIC_STRM_TYPE_ID::XZ);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////