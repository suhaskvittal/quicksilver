/*
    author: Suhas Vittal
    date:   2025 August 18
*/

#ifndef SIM_h
#define SIM_h

#include "instruction.h"
#include "sim/client.h"
#include "sim/routing.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// External simulation variable for logical cycles 
// `GL_CYCLE` is for compute, `GL_MEMORY_CYCLES` is for memory (which is assumed to be slower)
extern uint64_t GL_CYCLE; 
extern uint64_t GL_MEMORY_CYCLES:

class SIM
{
public:
    using inst_ptr = CLIENT::inst_ptr;
    
    struct PATCH
    {
        uint8_t                 client_id;
        qubit_type              qubit_id;
        ROUTING_BASE::ptr_type  bus;
        uint64_t                t_free{0};
    };
private:
    std::vector<CLIENT> clients_;
    
    // this is compute storage: can only perform operations on qubits in compute:
    std::vector<PATCH> q_compute_;

    double compute_speed_khz_;
public:
    SIM();

    void tick();
private:
    void tick_client(CLIENT&);
    void execute_instruction(inst_ptr);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif // SIM_h