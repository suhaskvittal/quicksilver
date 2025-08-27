/*
    author: Suhas Vittal
    date:   27 August 2025
*/

#include "compute.h"

constexpr size_t NUM_CCZ_UOPS = 13;
constexpr size_t NUM_CCX_UOPS = NUM_CCZ_UOPS+2;

std::mt19937 GL_RNG{0};
static std::uniform_real_distribution<double> FP_RAND{0.0, 1.0};

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE::COMPUTE(uint64_t t_sext_round_ns, size_t code_distance, CONFIG cfg, const std::vector<T_FACTORY*>& t_factories)
    :CLOCKABLE(compute_freq_khz(t_sext_round_ns, code_distance)),
    patches_((cfg.num_rows+1) * cfg.patches_per_row),
    t_fact_(t_factories)
{
    // number of patches reserved for resource pins are the number of factories at or higher than the target T factory level:
    patches_reserved_for_resource_pins_ = std::count_if(t_factories.begin(), t_factories.end(),
                                                        [lvl=target_t_fact_level_] (T_FACTORY* f) { return f->level >= lvl; });

    // initialize the routing space:
    auto [junctions, buses] = init_routing_space(cfg);

    // initialize the compute memory:
    init_compute(cfg, junctions, buses);

    // determine amount of program memory required:
    size_t total_qubits_required = std::transform_reduce(clients_.begin(), clients_.end(), 
                                            size_t{0}, 
                                            std::plus<size_t>{}, 
                                            [] (const client_ptr& c) { return c->qubits.size(); });
    size_t avail_patches = patches_.size() - patches_reserved_for_resource_pins_;
    if (avail_patches < total_qubits_required)
        throw std::runtime_error("Not enough space to allocate all program qubits");

    // initialize the clients
    init_clients(cfg);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

constexpr size_t NUM_STALL_TYPES{3};
// these are stall array indices:
constexpr size_t MEMORY_STALL_IDX{0};
constexpr size_t ROUTING_STALL_IDX{1};
constexpr size_t RESOURCE_STALL_IDX{2};

std::array<size_t, NUM_STALL_TYPES>
_count_stall_types(const std::vector<COMPUTE::EXEC_RESULT>& exec_results)
{
    auto _count = [&exec_results] (COMPUTE::EXEC_RESULT r) 
                    { 
                        return std::count_if(exec_results.begin(), exec_results.end(),
                                 [r] (COMPUTE::EXEC_RESULT rr) { return rr == r; });
                    };
    std::array<size_t, NUM_STALL_TYPES> stall_counts{};
    stall_counts[MEMORY_STALL_IDX] = _count(COMPUTE::EXEC_RESULT::MEMORY_STALL);
    stall_counts[ROUTING_STALL_IDX] = _count(COMPUTE::EXEC_RESULT::ROUTING_STALL);
    stall_counts[RESOURCE_STALL_IDX] = _count(COMPUTE::EXEC_RESULT::RESOURCE_STALL);
    return stall_counts;
}

void
COMPUTE::operate()
{
#if defined(QS_SIM_DEBUG)
    std::cout << "--------------------------------\n";
    std::cout << "COMPUTE CYCLE " << cycle_ << "\n";
#endif

    for (size_t i = 0; i < clients_.size(); i++)
    {
        client_ptr& c = clients_[i];
        exec_results_.clear();

#if defined(QS_SIM_DEBUG)
        std::cout << "CLIENT " << i 
                << " (trace = " << c->trace_file 
                << ", #qubits = " << c->qubits.size() 
                << ", inst done = " << c->s_inst_done  
                << ")\n";
#endif

        // 1. retire any instructions at the head of a window,
        //    or update the number of cycles until done
        client_try_retire(c);

        // 2. check if any instruction is ready to be executed. 
        //    An instruction is ready to be executed if it is at the head of all its arguments' windows.
        client_try_execute(c);

        // 3. check if any instructions can be fetched (read from trace file)
        //    This is done if any qubit has an empty window.
        client_try_fetch(c);

        // update stats using `exec_results_`:
        // if there is any success, then there is no stall:
        bool any_success = std::any_of(exec_results_.begin(), exec_results_.end(),
                                       [] (EXEC_RESULT r) { return r == EXEC_RESULT::SUCCESS; });
        if (!any_success)
        {
            auto stall_counts = _count_stall_types(exec_results_);
            c->s_cycles_stalled++;
            c->s_cycles_stalled_by_mem += (stall_counts[MEMORY_STALL_IDX] > 0);
            c->s_cycles_stalled_by_routing += (stall_counts[ROUTING_STALL_IDX] > 0);
            c->s_cycles_stalled_by_resource += (stall_counts[RESOURCE_STALL_IDX] > 0);
        }
    }

#if defined(QS_SIM_DEBUG)
    // print factory information:
    for (auto* f : t_fact_)
        std::cout << "FACTORY L" << f->level << " (buffer_occu = " << f->buffer_occu << ", step = " << f->step << ")\n";
#endif
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
_update_instruction_and_check_if_done(COMPUTE::inst_ptr inst, uint64_t cycle)
{
    if (inst->cycle_done <= cycle)
    {
        return true;
    }
    else
    {
        inst->cycle_done--;
        return false;
    }
}

void
_retire_instruction(COMPUTE::client_ptr& c, COMPUTE::inst_ptr inst)
{
    // update stats:
    c->s_inst_done++;
    if (inst->num_uops > 0)
        c->s_unrolled_inst_done += inst->num_uops;
    else
        c->s_unrolled_inst_done++;

    // remove the instruction from all windows it is in
    for (qubit_type qid : inst->qubits)
    {
        auto& q = c->qubits[qid];
        if (q.inst_window.front() != inst)
        {
            throw std::runtime_error("instruction `" + inst->to_string() 
                                    + "` is not at the head of qubit " 
                                    + std::to_string(qid) + " window");
        }
        q.inst_window.pop_front();
    }
    delete inst;
}


void
COMPUTE::client_try_retire(client_ptr& c)
{
    // retire any instructions at the head of a window,
    // or update the number of cycles until done
    for (auto& q : c->qubits)
    {
        if (q.inst_window.empty())
            continue;

        inst_ptr& inst = q.inst_window.front();
        // if the instruction has uops, we need to retire them.
        // once there are no uops left, we can retire the instruction.
        if (inst->num_uops > 0)
        {
            if (inst->curr_uop != nullptr && _update_instruction_and_check_if_done(inst->curr_uop, cycle_))
            {
                delete inst->curr_uop;
                inst->curr_uop = nullptr;
                inst->uop_completed++;
                inst->is_running = false;
                if (inst->uop_completed == inst->num_uops)
                    _retire_instruction(c, inst);
            }
        }
        else if (_update_instruction_and_check_if_done(inst, cycle_))
        {
            _retire_instruction(c, inst);
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE::client_try_execute(client_ptr& c)
{
    // check if any instruction is ready to be executed. 
    //    An instruction is ready to be executed if it is at the head of all its arguments' windows.
    for (auto& q : c->qubits)
    {
        if (q.inst_window.empty())
            continue;

        inst_ptr& inst = q.inst_window.front();

#if defined(QS_SIM_DEBUG)
        // verify that all arguments have a nonempty instruction window:
        for (qubit_type qid : inst->qubits)
        {
            if (c->qubits[qid].inst_window.empty())
                throw std::runtime_error("instruction `" + inst->to_string() 
                                        + "`: qubit " + std::to_string(qid) + " has an empty instruction window");
        }

        std::cout << "\tfound ready instruction: " << (*inst) << ", args ready =";

        for (qubit_type qid : inst->qubits)
            std::cout << " " << static_cast<int>(c->qubits[qid].inst_window.front() == inst);

        std::cout << ", is running = " << static_cast<int>(inst->is_running) 
                << "\n";
#endif

        bool all_ready = std::all_of(inst->qubits.begin(), inst->qubits.end(), 
                                    [&c, &inst] (qubit_type id) 
                                    {
                                        return c->qubits[id].inst_window.front() == inst;
                                    });

        if (all_ready && !inst->is_running)
        {
            auto result = execute_instruction(c, inst);
            exec_results_.push_back(result);
            
#if defined(QS_SIM_DEBUG)
            constexpr std::string_view RESULT_STRINGS[]
            {
                "SUCCESS", "MEMORY_STALL", "ROUTING_STALL", "RESOURCE_STALL", "WAITING_FOR_QUBIT_TO_BE_READY"
            };

            std::cout << "\t\tresult: " << RESULT_STRINGS[static_cast<size_t>(result)] << "\n";
            if (result == EXEC_RESULT::SUCCESS)
            {
                std::cout << "\t\twill be done @ cycle " << inst->cycle_done 
                            << ", uops = " << inst->uop_completed << " of " << inst->num_uops << "\n";
                if (inst->curr_uop != nullptr)
                {
                    std::cout << "\t\t\tcurr uop: " << (*inst->curr_uop)
                        << "\n\t\t\tuop will be done @ cycle: " << inst->curr_uop->cycle_done << "\n";
                }
            }
#endif
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE::client_try_fetch(client_ptr& c)
{
#if defined(QS_SIM_DEBUG)
    // print instruction window states:
    std::cout << "\tINSTRUCTION WINDOW:\n";
    for (size_t i = 0; i < std::min(c->qubits.size(), size_t{10}); i++)
    {
        if (!c->qubits[i].inst_window.empty())
        {
            std::cout << "\t\tQUBIT " << i << ": " 
                    << *(c->qubits[i].inst_window.front()) 
                    << " (count = " << c->qubits[i].inst_window.size() << ")\n";
        }
    }
#endif

    // read instructions from the trace and add them to instruction windows.
    // stop when all windows have at least one instruction.
    auto it = std::find_if(c->qubits.begin(), c->qubits.end(),
                           [] (const auto& q) { return q.inst_window.empty(); });
    qubit_type target_qubit = std::distance(c->qubits.begin(), it);

#if defined(QS_SIM_DEBUG)
    std::cout << "\tsearching for instructions that operate on qubit " << target_qubit << "\n";
#endif

    size_t limit{8};
    while (it != c->qubits.end() && limit > 0)
    {
        while (limit > 0) // keep going until we get an instruction that operates on `target_qubit`
        {
            inst_ptr inst{new INSTRUCTION(c->read_instruction_from_trace())};
            c->s_inst_read++;
            
#if defined(QS_SIM_DEBUG)
            std::cout << "\t\tREAD instruction: " << (*inst) << "\n";
#endif

            // set the number of uops (may depend on simulator config)
            if (inst->type == INSTRUCTION::TYPE::RX || inst->type == INSTRUCTION::TYPE::RZ)
                inst->num_uops = inst->urotseq.size();
            else if (inst->type == INSTRUCTION::TYPE::CCX || inst->type == INSTRUCTION::TYPE::CCZ)
                inst->num_uops = inst->type == INSTRUCTION::TYPE::CCX ? NUM_CCX_UOPS : NUM_CCZ_UOPS;
            else
                inst->num_uops = 0;

            // add the instruction to the windows of all the qubits it operates on
            for (qubit_type q : inst->qubits)
                c->qubits[q].inst_window.push_back(inst);

            // check if the instruction operates on `target_qubit`
            auto qubits_it = std::find(inst->qubits.begin(), inst->qubits.end(), target_qubit);
            if (qubits_it != inst->qubits.end())
                break;

            limit--;
        }

        // check again for any empty windows
        it = std::find_if(c->qubits.begin(), c->qubits.end(),
                           [] (const auto& q) { return q.inst_window.empty(); });
    }   
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
is_software_instruction(INSTRUCTION::TYPE type)
{
    return type == INSTRUCTION::TYPE::X 
        || type == INSTRUCTION::TYPE::Y
        || type == INSTRUCTION::TYPE::Z
        || type == INSTRUCTION::TYPE::SWAP;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim