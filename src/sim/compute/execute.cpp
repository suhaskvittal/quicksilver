/* 
    author: Suhas Vittal
    date:   27 August 2025

    This file contains the implementation of `execute_instruction` for `COMPUTE`.
*/

#include "sim/compute.h"

constexpr size_t NUM_CCZ_UOPS = 13;
constexpr size_t NUM_CCX_UOPS = NUM_CCZ_UOPS+2;

extern std::mt19937 GL_RNG;

static std::uniform_real_distribution<double> FP_RAND{0.0, 1.0};

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE::EXEC_RESULT
COMPUTE::execute_instruction(client_ptr& c, inst_ptr inst)
{
    // complete immediately -- do not have to wait for qubits to be ready
    if (is_software_instruction(inst->type))
    {
        inst->cycle_done = cycle_ + 1;
        inst->is_running = true;
        return EXEC_RESULT::SUCCESS;
    }

    EXEC_RESULT result{EXEC_RESULT::SUCCESS};

    // check if all qubits are in compute memory:
    for (qubit_type qid : inst->qubits)
    {
        auto& q = c->qubits[qid];

        // ensure all instructions are ready:
        if (q.memloc_info.t_free > cycle_)
        {
            // if we are waiting for a qubit to return from memory, then this is a memory stall
            result = (q.memloc_info.where == MEMINFO::LOCATION::COMPUTE) 
                            ? EXEC_RESULT::WAITING_FOR_QUBIT_TO_BE_READY 
                            : EXEC_RESULT::MEMORY_STALL;
            break;
        }

        // if a qubit is not in memory, we need to make a memory request:
        if (q.memloc_info.where == MEMINFO::LOCATION::MEMORY)
        {
            // make memory request:
            MEMORY_MODULE::request_type req{c.get(), q.memloc_info.client_id, q.memloc_info.qubit_id};

            // find memory module containing qubit:
            auto m_it = find_memory_module_containing_qubit(q.memloc_info.client_id, q.memloc_info.qubit_id);
            if (m_it == memory_.end())
            {
                throw std::runtime_error("memory module not found for qubit " + std::to_string(q.memloc_info.qubit_id) 
                                            + " of client " + std::to_string(q.memloc_info.client_id));
            }
            m_it->request_buffer_.push_back(req);

            // set `t_until_in_compute` and `t_free` 
            // to `std::numeric_limits<uint64_t>::max()` to indicate that
            // this is blocked:
            q.memloc_info.t_free = std::numeric_limits<uint64_t>::max();

            result = EXEC_RESULT::MEMORY_STALL;
        }
    }

    // if any qubit is unavailable we need to exit:
    if (result != EXEC_RESULT::SUCCESS)
        return result;

#if defined(QS_SIM_DEBUG)
    std::cout << "\t\tall qubits are available -- trying to execute instruction: " << (*inst) << "\n";
#endif

    // if this is a gate that requires a resource, make sure the resource is available:
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

        // get qubit and its patch:
        auto& q = c->qubits[inst->qubits[0]];
        
        // get qubit patch:
        const auto& q_patch = *find_patch_containing_qubit(q.memloc_info.client_id, q.memloc_info.qubit_id);

        // check if there is a free bus next to the patch:
#if defined(QS_SIM_DEBUG)
        std::cout << "\t\tbuses near qubit " << inst->qubits[0] << " (patch = " << q_patch.client_id << ", " << q_patch.qubit_id << "):";
        for (auto& b : q_patch.buses)
            std::cout << " " << b->t_free;
        std::cout << "\n";
#endif

        auto it = find_free_bus(q_patch);
        if (it == q_patch.buses.end())
        {
            result = EXEC_RESULT::ROUTING_STALL;
        }
        else
        {
            q.memloc_info.t_free = cycle_ + 2;
            (*it)->t_free = cycle_ + 2;
            inst->cycle_done = cycle_ + 2;
            inst->is_running = true;
        }
    }
    else if (inst->type == INSTRUCTION::TYPE::CX)
    {
        // this is a 2-cycle gate that requires the bus:
        // As we allocate an ancilla on the bus that needs to connect to the control and target
        // qubits, we need to route from the control to the target and occupy all bus components.
        // on the path.
        auto& ctrl = c->qubits[inst->qubits[0]];
        auto& target = c->qubits[inst->qubits[1]];

        const auto& c_patch = *find_patch_containing_qubit(ctrl.memloc_info.client_id, ctrl.memloc_info.qubit_id);
        const auto& t_patch = *find_patch_containing_qubit(target.memloc_info.client_id, target.memloc_info.qubit_id);

        if (alloc_routing_space(c_patch, t_patch, 2, 2))
        {
            inst->cycle_done = cycle_ + 2;
            inst->is_running = true;

            ctrl.memloc_info.t_free = cycle_ + 2;
            target.memloc_info.t_free = cycle_ + 2;
        }
        else
        {
            result = EXEC_RESULT::ROUTING_STALL;
        }
    }
    else if (inst->type == INSTRUCTION::TYPE::T 
        || inst->type == INSTRUCTION::TYPE::TDG
        || inst->type == INSTRUCTION::TYPE::TX
        || inst->type == INSTRUCTION::TYPE::TXDG)
    {
        // with 50% probability, we need to apply a clifford correction.
        // This is a S or SX gate -- either way, takes 2 cycles)
        // no need to actually simulate the S/SX gate, just add extra latency.
        bool clifford_correction = FP_RAND(GL_RNG) < 0.5;
        const uint64_t endpoint_latency = clifford_correction ? 4 : 2;
        const uint64_t path_latency = 2;

        auto& q = c->qubits[inst->qubits[0]];
        const auto& q_patch = *find_patch_containing_qubit(q.memloc_info.client_id, q.memloc_info.qubit_id);

        // keep trying until we succeed or there is no factory that has a resource state:
        bool any_factory_has_resource{false};
        for (size_t i = 0; i < t_fact_.size() && !inst->is_running; i++)
        {
            auto* fact = t_fact_[i];
            if (fact->level != target_t_fact_level_ || fact->buffer_occu == 0)
                continue;

            any_factory_has_resource = true;
            // get the factory's output patch and consume the magic state:
            const auto& f_patch = patch(fact->output_patch_idx);

            if (alloc_routing_space(f_patch, q_patch, endpoint_latency, path_latency))
            {
                inst->cycle_done = cycle_ + 2 + 2*static_cast<int>(clifford_correction);
                inst->is_running = true;

                q.memloc_info.t_free = cycle_ + 2 + 2*static_cast<int>(clifford_correction);

                fact->buffer_occu--;

                result = EXEC_RESULT::SUCCESS;
            }
            else
            {
                result = EXEC_RESULT::ROUTING_STALL;
            }
        }

        if (!any_factory_has_resource)
            result = EXEC_RESULT::RESOURCE_STALL;
    }
    else if (inst->type == INSTRUCTION::TYPE::RX || inst->type == INSTRUCTION::TYPE::RZ)
    {
        // create uop and run it:
        if (inst->curr_uop == nullptr)
        {
            size_t uop_idx = inst->uop_completed;
            inst->curr_uop = new INSTRUCTION(inst->urotseq[uop_idx], inst->qubits);
        }
        result = execute_instruction(c, inst->curr_uop);
        inst->is_running = (result == EXEC_RESULT::SUCCESS);
    }
    else if (inst->type == INSTRUCTION::TYPE::CCX || inst->type == INSTRUCTION::TYPE::CCZ)
    {
        using uop_spec_type = std::pair<INSTRUCTION::TYPE, std::array<ssize_t, 2>>;

        // gate declaration to make this very simple:
        constexpr INSTRUCTION::TYPE CX = INSTRUCTION::TYPE::CX;
        constexpr INSTRUCTION::TYPE TDG = INSTRUCTION::TYPE::TDG;
        constexpr INSTRUCTION::TYPE T = INSTRUCTION::TYPE::T;
        constexpr uop_spec_type CCZ_UOPS[]
        {
            {CX, {1,2}},
            {TDG, {2,-1}},
            {CX, {0,2}},
            {T, {2,-1}},
            {CX, {1,2}},
            {T, {1,-1}},
            {TDG, {2,-1}},
            {CX, {0,2}},
            {T, {2,-1}},
            {CX, {0,1}},
            {T, {0,-1}},
            {TDG, {1,-1}},
            {CX, {0,1}}
        };

        // depending on simulator config, we will want to use magic states or synthillation

        // T state implementation:
        int64_t uop_idx = static_cast<int64_t>(inst->uop_completed);
        if (inst->curr_uop == nullptr)
        {
            if (inst->type == INSTRUCTION::TYPE::CCX)
            {
                if (uop_idx == 0 || uop_idx == NUM_CCX_UOPS-1)
                {
                    inst->curr_uop = new INSTRUCTION(INSTRUCTION::TYPE::H, {inst->qubits[2]});
                }
                else
                {
                    std::vector<qubit_type> qubits;
                    const auto& uop = CCZ_UOPS[uop_idx-1];
                    for (ssize_t idx : uop.second)
                    {
                        if (idx >= 0)
                            qubits.push_back(inst->qubits[idx]);
                    }
                    inst->curr_uop = new INSTRUCTION(uop.first, qubits);
                }
            }
            else
            {
                std::vector<qubit_type> qubits;
                const auto& uop = CCZ_UOPS[uop_idx];
                for (ssize_t idx : uop.second)
                {
                    if (idx >= 0)
                        qubits.push_back(inst->qubits[idx]);
                }
                inst->curr_uop = new INSTRUCTION(uop.first, qubits);
            }
        }

        result = execute_instruction(c, inst->curr_uop);
        inst->is_running = (result == EXEC_RESULT::SUCCESS);
    }
    else if (inst->type == INSTRUCTION::TYPE::MZ || inst->type == INSTRUCTION::TYPE::MX)
    {
        // takes one cycle to complete, and doesn't require any routing/resources
        inst->cycle_done = cycle_ + 1;
        inst->is_running = true;
    }
    else
    {
        throw std::runtime_error("unsupported instruction: " + inst->to_string());
    }
        
    return result;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim