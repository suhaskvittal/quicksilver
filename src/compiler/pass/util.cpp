/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#include "compiler/pass/util.h"

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

IO_UTILITY::IO_UTILITY(generic_strm_type& _istrm, generic_strm_type& _ostrm)
    :num_qubits(_read_qubits_and_copy_to_ostrm(_istrm, _ostrm)),
    istrm(_istrm),
    ostrm(_ostrm)
{
    outgoing_buffer_.reserve(OUTGOING_CAPACITY);
}

IO_UTILITY::~IO_UTILITY()
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
IO_UTILITY::read_instructions(DAG* d, size_t max_capacity)
{
    while (d->inst_count() < max_capacity && !generic_strm_eof(istrm))
    {
        inst_ptr inst = read_instruction_from_stream(istrm);
        if (inst != nullptr)
            d->add_instruction(inst);
    }
}

void
IO_UTILITY::write_instruction(inst_ptr inst)
{
    outgoing_buffer_.push_back(inst);
    if (outgoing_buffer_.size() >= OUTGOING_CAPACITY)
        drain_outgoing_buffer();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
IO_UTILITY::drain_outgoing_buffer()
{
    auto begin = outgoing_buffer_.begin(),
         end = outgoing_buffer_.begin() + OUTGOING_CAPACITY/2;
    std::for_each(begin, end,
            [this] (auto* inst)
            {
                write_instruction_to_stream(ostrm, inst);
                delete inst;
            });
    // shift data and erase afterward
    std::move(end, outgoing_buffer_.end(), begin);
    outgoing_buffer_.erase(end, outgoing_buffer_.end());
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
