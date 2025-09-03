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
    using client_qubit_type = std::pair<int8_t, qubit_type>;  // client_id, qubit_id     
    constexpr client_qubit_type UNITIALIZED{-1,-1};

    // the contents of the memory
    std::vector<client_qubit_type> contents;

    // module pin in the compute memory
    size_t patch_idx{0};

    MEMORY_MODULE(double freq_ghz, size_t capacity);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class MEMORY
{
public:
    struct request_type
    {
        bool completed{false};
        CLIENT* client;
        int8_t client_id;
        qubit_type requested_qubit;
    };

    using request_buffer_type = std::vector<request_type>;
private:
    std::vector<MEMORY_MODULE*> modules_;
    
    // request buffer
    request_buffer_type request_buffer_;

    // this is a pointer to `compute` (so we can perform a move)
    COMPUTE* compute_;
public:
    MEMORY(const std::vector<MEMORY_MODULE*>& modules, COMPUTE*);    

    void operate() override;

    const std::vector<MEMORY_MODULE*>& modules() const { return modules_; }
private:
    using qubit_search_result_type = std::pair<std::vector<MEMORY_MODULE*>::iterator, 
                                               std::vector<MEMORY_MODULE::client_qubit_type>::iterator>;

    request_buffer_type::iterator find_ready_request();
    request_buffer_type::iterator find_ready_request(request_buffer_type::iterator from);
    bool                          serve_request(request_buffer_type::iterator req_it);
    qubit_search_result_type      search_for_qubit(int8_t client_id, qubit_type qubit_id);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}

#endif // SIM_MEMORY_h