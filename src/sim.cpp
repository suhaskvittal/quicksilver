/*
    author: Suhas Vittal
    date:   2025 August 18
*/

#include "sim.h"

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
SIM::tick()
{
    // tick each client:
    for (CLIENT& c : clients_)
        tick_client(c);

    // tick magic state factories:

    // tick the memory:

    ++GL_CYCLE;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
SIM::tick_client(CLIENT& c)
{
    // 1. retire any instructions at the head of a window,
    //    or update the number of cycles until done
    for (auto& [qubit, inst_window] : c.qubit_inst_windows)
    {
        if (inst_window.empty())
            continue;

        inst_ptr& inst = inst_window.front();
        if (inst->cycles_until_done == 0)
        {
            delete inst_window.front();
            inst_window.pop_front();
            c.s_inst_done++;
        }
        else
        {
            inst->cycles_until_done--;
        }
    }

    // 2. check if any instruction is ready to be executed. 
    //    An instruction is ready to be executed if it is at the head of all its arguments' windows.
    for (auto& [qubit, inst_window] : c.qubit_inst_windows)
    {
        if (inst_window.empty())
            continue;

        inst_ptr& inst = inst_window.front();
        bool all_ready = std::all_of(inst->qubits.begin(), inst->qubits.end(), 
                                    [&inst] (qubit_type q) 
                                    {
                                        return c.qubit_inst_windows[q].front() == inst;
                                    });
        // TODO: if this is an instruction that requires a resource state, we need to check
        // that the resource is available.

        // TODO: we may need to decompose the instruction

        if (all_ready)
            execute_instruction(inst);
    }

    // 3. check if any instructions can be fetched (read from trace file)
    //    This is done if any qubit has an empty window.
    auto it = std::find_if(c.qubit_inst_windows.begin(), c.qubit_inst_windows.end(),
                           [] (auto& [qubit, inst_window]) { return inst_window.empty(); });
    while (it != c.qubit_inst_windows.end())
    {
        qubit_type target_qubit = it->first;
        while (true) // keep going until we get an instruction that operates on `target_qubit`
        {
            inst_ptr inst{new INSTRUCTION(c.read_instruction_from_trace())};
            
            // add the instruction to the windows of all the qubits it operates on
            for (qubit_type q : inst->qubits)
                c.qubit_inst_windows[q].push_back(inst);

            // check if the instruction operates on `target_qubit`
            auto qubits_it = std::find(inst->qubits.begin(), inst->qubits.end(), target_qubit);
            if (qubits_it != inst->qubits.end())
                break;
        }

        // check again for any empty windows
        it = std::find_if(c.qubit_inst_windows.begin(), c.qubit_inst_windows.end(),
                           [] (auto& [qubit, inst_window]) { return inst_window.empty(); });
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
SIM::execute_instruction(uint8_t client_id, inst_ptr inst)
{
    std::vector<QUBIT> qubits;
    std::transform(inst->qubits.begin(), inst->qubits.end(), std::back_inserter(qubits),
                    [this, client_id] (qubit_type q) 
                    { 
                        return this->q_compute_[client_id][q];
                    });

    if (inst->type == INSTRUCTION::TYPE::H 
        || inst->type == INSTRUCTION::TYPE::S
        || inst->type == INSTRUCTION::TYPE::SDG
        || inst->type == INSTRUCTION::TYPE::SX
        || inst->type == INSTRUCTION::TYPE::SXDG)
    {
        // these are all 2-cycle gates that require the bus:
        // H requires a rotation (extension)
        // S, SDG, etc. require an ancilla in Y basis (occupies bus) + Z/X basis merge, followed by ancilla measurement.
        //    A clifford correction is required afterward to ensure correctness, but this is always a software
        //    instruction. 

        // check if the bus is free
        QUBIT& q = qubits[0];
        if (q.bus->occupied)
            return;

        // otherwise occupy the bus and set the instruction's cycle until done to `GL_CYCLE + 2`
        q.bus->occupied = true;
        inst->cycles_until_done = GL_CYCLE + 2;
    }
    else if (inst->type == INSTRUCTION::TYPE::CX)
    {
        // this is a 2-cycle gate that requires the bus:
        // As we allocate an ancilla on the bus that needs to connect to the control and target
        // qubits, we need to route from the control to the target and occupy all bus components.
        // on the path.
        QUBIT& ctrl = qubits[0];
        QUBIT& target = qubits[1];

        // check if the bus is immediately free
        if (ctrl.bus->occupied || target.bus->occupied)
            return;

        // now check if we can route:
        std::vector<ROUTING_BASE::ptr_type> path = route_path_from_src_to_dst(ctrl.bus, target.bus);
        for (auto& r : path)
            r->occupied = true;

        inst->cycles_until_done = GL_CYCLE + 2;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////