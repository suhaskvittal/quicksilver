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
    This is a logical block of memory
*/
struct MEMORY_MODULE : public CLOCKABLE
{
    using client_qubit_type = std::pair<CLIENT*, qubit_type>;  // client_id, qubit_id     
    using qubit_block_type = std::vector<client_qubit_type>;

    struct request_type
    {
        CLIENT*    client;
        int8_t     client_id;
        qubit_type requested_qubit;
    };

    using request_buffer_type = std::vector<request_type>;

    // module pin in the compute memory
    size_t output_patch_idx{0};

    // this is a pointer to `compute` (so we can perform a move)
    COMPUTE* compute_;

    // the contents of the memory -- tracked in software
    qubit_block_type contents;

    // request buffer -- this is an infinitely deep buffer as it is managed by software
    request_buffer_type request_buffer_;

    MEMORY_MODULE(double freq_ghz, size_t capacity);

    void operate() override;

    qubit_block_type::iterator find_qubit(int8_t client_id, qubit_type qubit_id);
    qubit_block_type::iterator find_uninitialized_qubit();

    request_buffer_type::iterator find_ready_request();
    bool                          serve_request(request_buffer_type::iterator req_it);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif // SIM_MEMORY_h