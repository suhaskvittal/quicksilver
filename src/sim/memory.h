/*
    author: Suhas Vittal
    date:   27 August 2025
*/

#ifndef SIM_MEMORY_h
#define SIM_MEMORY_h

#include "sim/clock.h"
#include "sim/compute.h"
#include "sim/meminfo.h"

#include <utility>
#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
    This is a group of logical memory blocks (i.e., QLDPC code blocks).

    A single block is represented by a memory "bank". Only one request can
    be served per cycle, but each bank can be accessed independently. So,
    for example, bank/block 0 can be accessed while bank/block 1 is serving
    a request from a prior cycle.

    All banks must be of the same code block. This is a simulator simplification,
    but also a likely hardware constraint, as different QEC codes require different
    connectivity.
*/
struct MEMORY_MODULE : public CLOCKABLE
{
    using client_qubit_type = std::pair<int8_t, qubit_type>;  // client_id, qubit_id     
    using bank_type = std::vector<client_qubit_type>;
    using qubit_lookup_result_type = std::pair<std::vector<bank_type>::iterator, bank_type::iterator>;

    struct request_type
    {
        uint64_t   inst_number;
        CLIENT*    client;
        int8_t     client_id;
        qubit_type requested_qubit;
    };

    using request_buffer_type = std::vector<request_type>;

    // number of banks
    const size_t num_banks_{0};

    // module pin in the compute memory
    size_t output_patch_idx{0};

    // this is a pointer to `compute` (so we can perform a move)
    COMPUTE* compute_;

    // the contents of the memory -- tracked in software
    std::vector<bank_type> banks_;

    // request buffer -- this is an infinitely deep buffer as it is managed by software
    request_buffer_type request_buffer_;

    MEMORY_MODULE(double freq_ghz, size_t num_banks, size_t capacity_per_bank);

    void operate() override;

    qubit_lookup_result_type find_qubit(int8_t client_id, qubit_type qubit_id);
    qubit_lookup_result_type find_uninitialized_qubit();

    bool try_and_serve_request(request_buffer_type::iterator req_it);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif // SIM_MEMORY_h