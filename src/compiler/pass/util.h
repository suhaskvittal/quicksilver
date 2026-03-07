/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#ifndef COMPILER_PASS_UTIL_h
#define COMPILER_PASS_UTIL_h

#include "dag.h"
#include "generic_io.h"

#include <memory>
#include <unordered_set>
#include <vector>

namespace compiler
{
namespace pass
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * This is a utility class for managing IO during pass execution.
 * */
class IO_UTILITY
{
public:
    constexpr static size_t OUTGOING_CAPACITY{16384};

    using fixed_buffer_type = std::vector<DAG::inst_ptr>;

    const size_t num_qubits;
private:
    generic_strm_type& istrm;
    generic_strm_type& ostrm;

    /*
     * Buffer of instructions to be written. Once the buffer is full,
     * half of the entries are written.
     * */
    fixed_buffer_type outgoing_buffer_;
public:
    IO_UTILITY(generic_strm_type& istrm, generic_strm_type& ostrm);

    /*
     * Note: `IO_UTILITY` does not close either `istrm` or `ostrm`.
     * It only drains `outgoing_buffer_` on deletion.
     * */
    ~IO_UTILITY();

    /*
     * Reads instructions from `istrm` and adds then to the DAG.
     * Stops when the DAG reaches `max_capacity`.
     * */
    void read_instructions(DAG*, size_t max_capacity);

    /*
     * Adds an instruction to `outgoing_buffer_`. If necessary,
     * the buffer is drained.
     * */
    void write_instruction(DAG::inst_ptr);

    template <class ITER>
    void write_instructions(ITER begin, ITER end);
private:
    void drain_outgoing_buffer();
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Implementation of `IO_UTILITY::write_instructions()`
 * */

template <class ITER> void
IO_UTILITY::write_instructions(ITER begin, ITER end)
{
    size_t remaining_capacity = OUTGOING_CAPACITY - outgoing_buffer_.size();
    size_t d = std::distance(begin, end);
    if (d > remaining_capacity)
    {
        // we cannot add the entire range at once:
        auto _end = begin + remaining_capacity;
        outgoing_buffer_.insert(outgoing_buffer_.end(), begin, _end);
        drain_outgoing_buffer();
        outgoing_buffer_.insert(outgoing_buffer_.end(), _end, end);
    }
    else
    {
        outgoing_buffer_.insert(outgoing_buffer_.end(), begin, end);
    }

    if (outgoing_buffer_.size() >= OUTGOING_CAPACITY)
        drain_outgoing_buffer();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace pass
}  // namespace compiler

#endif  // COMPILER_PASS_UTIL_h
