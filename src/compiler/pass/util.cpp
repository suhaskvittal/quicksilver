/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#include "compiler/pass/util.h"

#include <algorithm>

namespace compiler
{
namespace pass
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using inst_ptr = DAG::inst_ptr;

uint32_t _read_qubits_and_copy_to_ostrm(generic_strm_type& istrm, generic_strm_type& ostrm);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

IOUtility::IOUtility(generic_strm_type& _istrm, generic_strm_type& _ostrm)
    :num_qubits(_read_qubits_and_copy_to_ostrm(_istrm, _ostrm)),
    istrm(_istrm),
    ostrm(_ostrm)
{
    outgoing_buffer_.reserve(OUTGOING_CAPACITY);
}

IOUtility::~IOUtility()
{
    for (auto* inst : outgoing_buffer_)
    {
        write_instruction_to_stream(ostrm, inst);
        delete inst;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
IOUtility::read_instructions(DAG* d, size_t max_capacity)
{
    while (d->inst_count() < max_capacity && !generic_strm_eof(istrm))
    {
        inst_ptr inst = read_instruction_from_stream(istrm);
        if (inst != nullptr)
            d->add_instruction(inst);
    }
}

void
IOUtility::write_instruction(inst_ptr inst)
{
    outgoing_buffer_.push_back(inst);
    if (outgoing_buffer_.size() >= OUTGOING_CAPACITY)
        drain_outgoing_buffer();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
IOUtility::drain_outgoing_buffer()
{
    // Write out (and free) the oldest `min(OUTGOING_CAPACITY/2, size())` entries. The `min`
    // makes this correct regardless of how large `outgoing_buffer_` actually is at the point
    // this is called -- `write_instructions()`'s bulk-range overload can leave the buffer
    // holding more than `OUTGOING_CAPACITY` entries when it calls this, and a fixed
    // `begin + OUTGOING_CAPACITY/2` end iterator in that case used to erase part of the
    // surviving tail (rather than only the entries just written) without writing or freeing
    // it -- a silent loss + leak. `vector::erase` shifts the remaining tail down and resizes
    // correctly for any size, so hand-rolling that with `std::move` isn't needed.
    auto begin = outgoing_buffer_.begin();
    auto end   = begin + std::min(OUTGOING_CAPACITY/2, outgoing_buffer_.size());
    std::for_each(begin, end,
            [this] (auto* inst)
            {
                write_instruction_to_stream(ostrm, inst);
                delete inst;
            });
    outgoing_buffer_.erase(begin, end);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

uint32_t
_read_qubits_and_copy_to_ostrm(generic_strm_type& istrm, generic_strm_type& ostrm)
{
    uint32_t q;
    generic_strm_read(istrm, &q, sizeof(q));
    generic_strm_write(ostrm, &q, sizeof(q));
    return q;
}

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace pass
}  // namespace compiler
