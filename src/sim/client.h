/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#ifndef SIM_CLIENT_h
#define SIM_CLIENT_h

#include "dag.h"
#include "generic_io.h"
#include "globals.h"

#include <memory>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class CLIENT
{
public:
    using inst_ptr = DAG::inst_ptr;
    /*
     * Statistics (only variables prefixed by `s_`)
     * */
    uint64_t s_inst_read{0};
    uint64_t s_inst_done{0};
    uint64_t s_unrolled_inst_done{0};

    const std::string    trace_file;
    const client_id_type id;
    const size_t         num_qubits;
private:
    std::unique_ptr<DAG> dag_;
    generic_strm_type    tristrm_;
    bool                 has_hit_eof_once_{false};
public:
    CLIENT(std::string trace_file, int8_t id);
    ~CLIENT();

    std::vector<inst_ptr> get_ready_instructions();
    void                  retire_instruction(inst_ptr);

    bool eof() const;
private:
    size_t   open_file_and_read_qubit_count();
    inst_ptr read_instruction_from_trace();
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif  // SIM_CLIENT_h
