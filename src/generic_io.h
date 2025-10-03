/*
    author: Suhas Vittal
    date:   23 September 2025
*/

#ifndef GENERIC_IO_h
#define GENERIC_IO_h

#include <cstdio>
#include <string>
#include <variant>

#include <lzma.h>
#include <zlib.h>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
    LZMA support:
*/
class LZMA_FILE
{
private:
    constexpr static size_t LZMA_BUF_SIZE{4096};

    lzma_stream  lzma_strm;
    FILE*        file_istrm;
    char         lzma_buf[LZMA_BUF_SIZE];

    bool is_open{true};
public: 
    LZMA_FILE(FILE*);
    ~LZMA_FILE();

    size_t read(void*, size_t);
    bool   eof() const;
    void   close();
private:
    void read_chunk_from_file();
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

using generic_strm_type = std::variant<FILE*, gzFile, LZMA_FILE*>;

enum class GENERIC_STRM_TYPE_ID { FILE, GZ, XZ };

void generic_strm_open(generic_strm_type& strm, std::string file_path, std::string mode);
size_t generic_strm_read(generic_strm_type& strm, void* buf, size_t size);
void generic_strm_write(generic_strm_type& strm, void* buf, size_t size);
void generic_strm_close(generic_strm_type& strm);
bool generic_strm_eof(const generic_strm_type& strm);
bool generic_strm_is_for_compressed_file(const generic_strm_type& strm);

void generic_strm_seek(generic_strm_type& strm, size_t offset, int whence);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif