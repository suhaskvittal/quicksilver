/*
 *  author: Suhas Vittal
 *  date:   27 June 2026
 * */

#ifndef COMPILER_PASS_DECODER_ALLOCATOR_h
#define COMPILER_PASS_DECODER_ALLOCATOR_h

#include "generic_io.h"

#include <cstdint>
#include <string>

namespace compiler
{
namespace pass
{
namespace decoder_alloc
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct DecoderInfo
{
    std::string name;
    uint64_t    reaction_time;
    size_t      total_count;
};

struct Config
{
    DecoderInfo fast_decoder;
    DecoderInfo slow_decoder;
    uint64_t    syndrome_cycle_time{1};
};

struct Stats
{
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class DecoderAllocator>
Stats run(generic_strm_type& ostrm, generic_strm_type& istrm, const DecoderAllocator&, Config);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace decoder_alloc
} // namespace pass
} // namespace compiler

#include "decoder_allocator.tpp"

#endif // COMPILER_PASS_DECODER_ALLOCATOR_h
