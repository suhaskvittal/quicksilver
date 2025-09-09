/*
    author: Suhas Vittal
    date:   27 August 2025

    `init_clients`, `init_routing_space`, `init_compute` are contained in `compute/init.cpp`
    `execute_instruction` is contained in `compute/execute.cpp`
*/

#include "sim/compute.h"
#include "sim/compute/replacement/lru.h"
#include "sim/compute/replacement/lti.h"
#include "sim/memory.h"

#include <unordered_set>

constexpr size_t NUM_CCZ_UOPS = 13;
constexpr size_t NUM_CCX_UOPS = NUM_CCZ_UOPS+2;

std::mt19937 GL_RNG{0};
static std::uniform_real_distribution<double> FP_RAND{0.0, 1.0};

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE::COMPUTE(double freq_ghz, 
                    const std::vector<std::string>& client_trace_files,
                    size_t num_rows, 
                    size_t num_patches_per_row, 
                    const std::vector<T_FACTORY*>& t_factories, 
                    const std::vector<MEMORY_MODULE*>& memory,
                    REPLACEMENT repl_id)
    :CLOCKABLE(freq_ghz),
    patches_(num_rows * num_patches_per_row + 2*(num_patches_per_row+2)),
    t_fact_(t_factories),
    memory_(memory)
{
    // configure `target_t_fact_level_`:
    auto max_fact_it = std::max_element(t_factories.begin(), t_factories.end(),
                                        [] (T_FACTORY* f1, T_FACTORY* f2) { return f1->level < f2->level; });
    target_t_fact_level_ = (*max_fact_it)->level;

    // number of patches reserved for resource pins are the number of factories at or higher than the target T factory level:
    size_t patches_reserved_for_resource_pins = std::count_if(t_factories.begin(), t_factories.end(),
                                                        [lvl=target_t_fact_level_] (T_FACTORY* f) { return f->level >= lvl; });
    size_t patches_reserved_for_memory_pins = memory_.size();

    patch_idx_compute_start_ = patches_reserved_for_resource_pins;
    patch_idx_memory_start_ = patches_reserved_for_resource_pins + num_rows*num_patches_per_row;

    // initialize the routing space + compute memory
    auto [junctions, buses] = init_routing_space(num_rows, num_patches_per_row);
    init_compute(num_rows, num_patches_per_row, junctions, buses);

    // determine amount of program memory required:
    size_t total_qubits_required = std::transform_reduce(clients_.begin(), clients_.end(), 
                                            size_t{0}, 
                                            std::plus<size_t>{}, 
                                            [] (const client_ptr& c) { return c->qubits.size(); });
    size_t avail_patches = patches_.size() - patches_reserved_for_resource_pins - patches_reserved_for_memory_pins;
    if (avail_patches < total_qubits_required)
        throw std::runtime_error("Not enough space to allocate all program qubits");

    // initialize the clients
    init_clients(client_trace_files);

    // connect memory to this compute:
    for (auto* m : memory_)
        m->compute_ = this;

    // initialize the replacement policy:
    if (repl_id == REPLACEMENT::LRU)
        repl_ = std::make_unique<compute::repl::LRU>(this);
    else if (repl_id == REPLACEMENT::LTI)
        repl_ = std::make_unique<compute::repl::LTI>(this);
    else
        throw std::runtime_error("Invalid replacement policy ID: " + std::to_string(static_cast<size_t>(repl_id)));
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE::operate()
{
#if defined(QS_SIM_DEBUG)
    if (cycle_ % QS_SIM_DEBUG_FREQ == 0)
    {
        std::cout << "--------------------------------\n";
        std::cout << "COMPUTE CYCLE " << cycle_ << "\n";
    }
#endif

    size_t ii = cycle_ % clients_.size();
    for (size_t i = 0; i < clients_.size(); i++)
    {
        client_ptr& c = clients_[ii];
        exec_results_.clear();

#if defined(QS_SIM_DEBUG)
        if (cycle_ % QS_SIM_DEBUG_FREQ == 0)
        {
            std::cout << "CLIENT " << c->id
                    << " (trace = " << c->trace_file 
                    << ", #qubits = " << c->qubits.size() 
                    << ", inst done = " << c->s_inst_done  
                    << ")\n";
        }
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
        exec_result_type cycle_stall{0};
        bool any_stalled = false;
        for (exec_result_type r : exec_results_)
        {
            c->s_inst_stalled += (r > 0);
            c->s_inst_stalled_by_type[r]++;
            any_stalled |= (r > 0);
            
            cycle_stall |= r;
        }
        c->s_cycles_stalled += (cycle_stall > 0);
        c->s_cycles_stalled_by_type[cycle_stall]++;

        ii = (ii+1) % clients_.size();
    }

#if defined(QS_SIM_DEBUG)
    // print factory information:
    if (cycle_ % QS_SIM_DEBUG_FREQ == 0)
    {
        for (auto* f : t_fact_)
            std::cout << "FACTORY L" << f->level << " (buffer_occu = " << f->buffer_occu << ", step = " << f->step << ")\n";

        // print memory information:
        for (size_t i = 0; i < memory_.size(); i++)
        {
            std::cout << "MEMORY MODULE " << i 
                        << " (num_banks = " << memory_[i]->num_banks_ 
                        << ", request buffer size = " << memory_[i]->request_buffer_.size() << ")\n";
        }
    }
#endif
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
COMPUTE::alloc_routing_space(const PATCH& src, const PATCH& dst, uint64_t block_endpoints_for, uint64_t block_path_for)
{
    // check if the bus is free
    if (src.buses.empty() || dst.buses.empty())
        throw std::runtime_error("source/destination has no buses at all");

    auto src_it = find_free_bus(src);
    auto dst_it = find_free_bus(dst);

    if (src_it == src.buses.end() || dst_it == dst.buses.end())
        return false;

    // now check if we can route:
    if (*src_it == *dst_it)
    {
        (*src_it)->t_free = cycle_ + block_endpoints_for;
    }
    else
    {
        auto path = route_path_from_src_to_dst(*src_it, *dst_it, cycle_);
        if (path.empty())
            return false;

        for (auto& r : path)
            r->t_free = cycle_ + block_path_for;

        // hold the endpoints for `endpoint_latency` cycles:
        (*src_it)->t_free = cycle_ + block_endpoints_for;
        (*dst_it)->t_free = cycle_ + block_endpoints_for;
    }

    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

CLIENT::qubit_info_type*
COMPUTE::select_victim_qubit(int8_t incoming_client_id, qubit_type incoming_qubit_id)
{
    const auto& incoming_client = clients_[incoming_client_id];
    const auto& incoming_qubit_info = incoming_client->qubits[incoming_qubit_id];
    auto victim =  repl_->select_victim(incoming_qubit_info);

    return victim;
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
    std::unordered_set<inst_ptr> visited;
    for (auto& q : c->qubits)
    {
        if (q.inst_window.empty())
            continue;

        inst_ptr inst = q.inst_window.front();
        if (visited.count(inst))
            continue;
        visited.insert(inst);

        bool all_ready = std::all_of(inst->qubits.begin(), inst->qubits.end(), 
                                    [&c, &inst] (qubit_type id) 
                                    {
                                        return c->qubits[id].inst_window.front() == inst;
                                    });

#if defined(QS_SIM_DEBUG)
        if (cycle_ % QS_SIM_DEBUG_FREQ == 0)
        {
            if (all_ready)
            {
                std::cout << "\tfound ready instruction: " << (*inst) << ", args ready =";

                for (qubit_type qid : inst->qubits)
                    std::cout << " " << static_cast<int>(c->qubits[qid].inst_window.front() == inst);

                std::cout << ", inst number = " << inst->inst_number 
                        << ", is running = " << static_cast<int>(inst->is_running) 
                        << ", cycle done = " << inst->cycle_done
                        << "\n";
            }
        }
#endif

        if (all_ready && !inst->is_running)
        {
            auto result = execute_instruction(c, inst);
            exec_results_.push_back(result);

            // update replacement policy on success
            if (result == EXEC_RESULT_SUCCESS)
            {
                for (qubit_type qid : inst->qubits)
                {
                    const auto& q = c->qubits[qid];
                    repl_->update_on_use(q);
                }
            }
            
#if defined(QS_SIM_DEBUG)
            if (cycle_ % QS_SIM_DEBUG_FREQ == 0)
            {
                constexpr std::string_view RESULT_STRINGS[]
                {
                    "SUCCESS", "MEMORY_STALL", "ROUTING_STALL", "RESOURCE_STALL", "WAITING_FOR_QUBIT_TO_BE_READY"
                };

                std::cout << "\t\tresult: " << RESULT_STRINGS[static_cast<size_t>(result)] << "\n";
                if (result == EXEC_RESULT_SUCCESS)
                {
                    std::cout << "\t\twill be done @ cycle " << inst->cycle_done 
                                << ", uops = " << inst->uop_completed << " of " << inst->num_uops << "\n";
                    if (inst->curr_uop != nullptr)
                    {
                        std::cout << "\t\t\tcurr uop: " << (*inst->curr_uop)
                            << "\n\t\t\tuop will be done @ cycle: " << inst->curr_uop->cycle_done << "\n";
                    }
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
    if (cycle_ % QS_SIM_DEBUG_FREQ == 0)
    {
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
    }
#endif

    // read instructions from the trace and add them to instruction windows.
    // stop when all windows have at least one instruction.
    auto it = std::find_if(c->qubits.begin(), c->qubits.end(),
                           [] (const auto& q) { return q.inst_window.empty(); });
    qubit_type target_qubit = std::distance(c->qubits.begin(), it);

#if defined(QS_SIM_DEBUG)
    if (cycle_ % QS_SIM_DEBUG_FREQ == 0)
        std::cout << "\tsearching for instructions that operate on qubit " << target_qubit << "\n";
#endif

    size_t limit{8};
    while (it != c->qubits.end() && limit > 0)
    {
        while (limit > 0) // keep going until we get an instruction that operates on `target_qubit`
        {
            inst_ptr inst{new INSTRUCTION(c->read_instruction_from_trace())};
            inst->inst_number = c->s_inst_read;
            c->s_inst_read++;
            
#if defined(QS_SIM_DEBUG)
            if (cycle_ % QS_SIM_DEBUG_FREQ == 0)
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

PATCH::bus_array::const_iterator
COMPUTE::find_free_bus(const PATCH& p) const
{
    return std::find_if(p.buses.begin(), p.buses.end(), 
                        [c=cycle_] (const auto& b) { return b->t_free <= c; });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<PATCH>::iterator
COMPUTE::find_patch_containing_qubit(int8_t client_id, qubit_type qubit_id)
{
    return std::find_if(patches_.begin(), patches_.end(),
                        [client_id, qubit_id] (const auto& p) { return p.client_id == client_id && p.qubit_id == qubit_id; });
}

std::vector<MEMORY_MODULE*>::iterator
COMPUTE::find_memory_module_containing_qubit(int8_t client_id, qubit_type qubit_id)
{
    return std::find_if(memory_.begin(), memory_.end(),
                        [client_id, qubit_id] (const auto& m) 
                        { 
                            auto [b_it, q_it] = m->find_qubit(client_id, qubit_id);
                            return b_it != m->banks_.end();
                        });
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