/*
    author: Suhas Vittal
    date:   23 September 2025
*/

#include "generic_io.h"

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
generic_strm_open(generic_strm_type& strm, std::string file_path, std::string mode)
{
    if (file_path.find(".gz") != std::string::npos)
        strm = gzopen(file_path.c_str(), mode.c_str());
    else
        strm = fopen(file_path.c_str(), mode.c_str());
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
generic_strm_read(generic_strm_type& strm, void* buf, size_t size)
{
    if (strm.index() == 0)
        fread(buf, 1, size, std::get<FILE*>(strm));
    else
        gzread(std::get<gzFile>(strm), buf, size);
}

void
generic_strm_write(generic_strm_type& strm, void* buf, size_t size)
{
    if (strm.index() == 0)
        fwrite(buf, 1, size, std::get<FILE*>(strm));
    else
        gzwrite(std::get<gzFile>(strm), buf, size);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
generic_strm_close(generic_strm_type& strm)
{
    if (strm.index() == 0)
        fclose(std::get<FILE*>(strm));
    else
        gzclose(std::get<gzFile>(strm));
}

bool
generic_strm_eof(const generic_strm_type& strm)
{
    if (strm.index() == 0)
        return feof(std::get<FILE*>(strm));
    else
        return gzeof(std::get<gzFile>(strm));
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////