/*
    author: Suhas Vittal
    date:   23 September 2025
*/

#ifndef GENERIC_IO_h
#define GENERIC_IO_h

#include <cstdio>
#include <string>
#include <variant>

#include <zlib.h>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

using generic_strm_type = std::variant<FILE*, gzFile>;

void generic_strm_open(generic_strm_type& strm, std::string file_path, std::string mode);
void generic_strm_read(generic_strm_type& strm, void* buf, size_t size);
void generic_strm_write(generic_strm_type& strm, void* buf, size_t size);
void generic_strm_close(generic_strm_type& strm);
bool generic_strm_eof(const generic_strm_type& strm);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif