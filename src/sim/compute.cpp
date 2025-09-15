/*
    author: Suhas Vittal
    date:   27 August 2025
*/

#include "sim/compute.h"
#include "compute.h"
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
    num_rows_(num_rows),
    num_patches_per_row_(num_patches_per_row),
    full_row_width_inc_ancilla_(num_patches_per_row_+1),
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
    // one patch per module:
    size_t patches_reserved_for_memory_pins = memory_.size();

    // bound the number of patches reserved for resource pins and memory pins -- can only take up one full row
    patches_reserved_for_resource_pins = std::max(patches_reserved_for_resource_pins, full_row_width_inc_ancilla_);
    patches_reserved_for_memory_pins = std::max(patches_reserved_for_memory_pins, full_row_width_inc_ancilla_);

    patch_idx_compute_start_ = patches_reserved_for_resource_pins;
    patch_idx_memory_start_ = patches_reserved_for_resource_pins + num_rows*num_patches_per_row;

    // initialize the routing space + compute memory
    auto [junctions, buses] = init_routing_space();
    init_compute(junctions, buses);

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

std::tuple<size_t, MEMORY_MODULE::qubit_lookup_result_type>
_find_module_with_uninitialized_qubit(const std::vector<MEMORY_MODULE*>& memory, size_t start_from_idx)
{
    for (size_t i = 0; i < memory.size(); i++)
    {
        size_t ii = (start_from_idx+i) % memory.size();
        auto lookup_result = memory[ii]->find_uninitialized_qubit();
        if (lookup_result.first != memory[ii]->banks_.end())
            return std::make_tuple(ii, lookup_result);
    }

    return std::make_tuple(memory.size(), MEMORY_MODULE::qubit_lookup_result_type{});
}

void
COMPUTE::init_clients(const std::vector<std::string>& client_trace_files)
{
    size_t patch_idx{patch_idx_compute_start_};

    // initialize all clients;
    for (size_t i = 0; i < client_trace_files.size(); i++)
    {
        client_ptr c{new sim::CLIENT(client_trace_files[i], i)};
        clients_.push_back(std::move(c));
    }

    // place qubits into compute memory (round robin fashion to be fair)
    int8_t client_id{0};
    std::vector<size_t> client_qubit_idx(clients_.size(), 0);

    // place qubits into memory once `patch_idx` reaches `patch_idx_memory_start_`:
    // do this in a round robin fashion to maximize module level and bank level parallelism
    size_t mem_idx{0};
    std::vector<size_t> mem_bank_idx(memory_.size(), 0);

    bool all_done{false};
    while (!all_done)
    {
        size_t idx = client_qubit_idx[client_id];
        client_ptr& c = clients_[client_id];

        if (idx >= c->qubits.size()) // update all done since this client is done
        {
            all_done = std::all_of(clients_.begin(), clients_.end(),
                                    [&client_qubit_idx] (const client_ptr& c) 
                                    { 
                                        return c->qubits.size() == client_qubit_idx[c->id]; 
                                    });
        }
        else
        {
            auto& q = c->qubits[idx];

            if (patch_idx >= patch_idx_memory_start_)
            {
                // place qubits into memory -- find a module with uninitialized qubit:
                MEMORY_MODULE::qubit_lookup_result_type lookup_result;
                std::tie(mem_idx, lookup_result) = _find_module_with_uninitialized_qubit(memory_, mem_idx);
                if (mem_idx == memory_.size())
                    throw std::runtime_error("Not enough space in memory to allocate all qubits");

                auto [b_it, q_it] = lookup_result;
                *q_it = std::make_pair(c->id, idx);
                q.memloc_info.where = MEMINFO::LOCATION::MEMORY;
                mem_idx = (mem_idx+1) % memory_.size();
            }
            else
            {
                // place in memory -- just need to set patch information:
                patches_[patch_idx].client_id = c->id;
                patches_[patch_idx].qubit_id = idx;
                patch_idx++;
            }
            client_qubit_idx[client_id]++;
        }

        client_id = (client_id+1) % clients_.size();
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
_connect(ROUTING_BASE::ptr_type j, ROUTING_BASE::ptr_type b)
{
    j->connections.push_back(b);
    b->connections.push_back(j);
}

COMPUTE::bus_info
COMPUTE::init_routing_space()
{
    // buses and junctions are arranged by pairs of rows:
    const size_t num_row_pairs = (num_rows_+1)/2;  // need to +1 to handle singleton row

    // number of junctions is num_row_pairs + 1
    // number of buses is 2 * num_row_pairs + 1
    const size_t num_junctions = num_row_pairs + 1;
    const size_t num_buses = 2 * num_row_pairs + 1;
    std::vector<sim::ROUTING_BASE::ptr_type> junctions(num_junctions);
    std::vector<sim::ROUTING_BASE::ptr_type> buses(num_buses);
    
    for (size_t i = 0; i < junctions.size(); i++)
        junctions[i] = sim::ROUTING_BASE::ptr_type(new sim::ROUTING_BASE{i, sim::ROUTING_BASE::TYPE::JUNCTION});

    for (size_t i = 0; i < buses.size(); i++)
        buses[i] = sim::ROUTING_BASE::ptr_type(new sim::ROUTING_BASE{i,sim::ROUTING_BASE::TYPE::BUS});

    for (size_t i = 0; i < num_row_pairs; i++)
    {
        //  i ----- 2i ------
        //  |                   
        // 2i+1
        //  |                   
        // i+1 --- 2i+2 -----
        _connect(junctions[i], buses[2*i]);
        _connect(junctions[i], buses[2*i+1]);
        _connect(junctions[i+1], buses[2*i+1]);
    }

    // and the last remaining bus + junction
    _connect(junctions[num_junctions-1], buses[num_buses-1]);

    return bus_info{junctions, buses};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE::init_compute(const bus_array& junctions, const bus_array& buses)
{
    // First setup the magic state pins:
    std::vector<sim::T_FACTORY*> top_level_t_fact;
    std::copy_if(t_fact_.begin(), t_fact_.end(), std::back_inserter(top_level_t_fact),
                [lvl=target_t_fact_level_] (T_FACTORY* f) { return f->level == lvl; });

    // connect the magic state factories (interleave if there are more top level factories than
    // space at the top)
    for (size_t i = 0; i < top_level_t_fact.size(); i++)
    {
        auto* fact = top_level_t_fact[i];

        // set the output patch index:
        fact->output_patch_idx = i % full_row_width_inc_ancilla_;  // this allows us to interleave the factories

        // create bus and junction connections:
        PATCH& fp = patches_[fact->output_patch_idx];
        if (fact->output_patch_idx == 0)
            fp.buses.push_back(junctions[0]);
        else
            fp.buses.push_back(buses[0]);
    }

    // now connect the program memory patches:
    for (size_t p = patch_idx_compute_start_; p < patch_idx_memory_start_; p++)
    {
        // get row idx and column idx:
        size_t r = p / num_patches_per_row_;
        size_t c = p % num_patches_per_row_;

        // compute row pair idx:
        size_t rp = (r+1)/2;

        // determine where the patch is so we can connect buses:
        bool is_upper = (r & 1) == 0;  // even rows are always upper
        bool is_lower = ((r & 1) == 1) || (r == num_rows_-1);  // odd rows are always lower, and the last row is always lower
        bool is_left = (c == 0);
    
        if (is_upper)
            patches_[p].buses.push_back(buses[2*rp]);
        if (is_left)
            patches_[p].buses.push_back(buses[2*rp+1]);
        if (is_lower)
            patches_[p].buses.push_back(buses[2*rp+2]);
    }

    // set the connections for the memory pins -- do same interleaving as for factories
    const auto& last_bus = buses.back();
    const auto& last_junction = junctions.back();
    for (size_t i = 0; i < memory_.size(); i++)
    {
        auto* m = memory_[i];
        m->output_patch_idx = (i % full_row_width_inc_ancilla_) + patch_idx_memory_start_;

        PATCH& mp = patches_[m->output_patch_idx];
        if (m->output_patch_idx == 0)
            mp.buses.push_back(last_junction);
        else
            mp.buses.push_back(last_bus);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
_update_instruction_and_check_if_done(COMPUTE::inst_ptr inst, uint64_t cycle)
{
    if (inst->cycle_done <= cycle)  { return true; }
    else                            { inst->cycle_done--; return false; }
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

            // handle renaming if this isn't a SWAP
            if (inst->type != INSTRUCTION::TYPE::SWAP)
            {
                for (auto& q : inst->qubits)
                    q = renamed_qubits_[c->id].count(q) ? renamed_qubits_[c->id][q] : q;
            }
            
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

COMPUTE::exec_result_type
COMPUTE::execute_instruction(client_ptr& c, inst_ptr inst)
{
    // complete immediately -- do not have to wait for qubits to be ready
    if (is_software_instruction(inst->type))
        return handle_sw_instruction(c, inst);

    exec_result_type result{0};

    // check if all qubits are in compute memory:
    for (qubit_type qid : inst->qubits)
    {
        auto& q = c->qubits[qid];

        // ensure all instructions are ready:
        if (q.memloc_info.t_free > cycle_)
        {
            // if we are waiting for a qubit to return from memory, then this is a memory stall
            result = (q.memloc_info.where == MEMINFO::LOCATION::COMPUTE) 
                            ? EXEC_RESULT_WAITING_FOR_QUBIT_TO_BE_READY 
                            : EXEC_RESULT_MEMORY_STALL;
            break;
        }

        // if a qubit is not in memory, we need to make a memory request:
        if (q.memloc_info.where == MEMINFO::LOCATION::MEMORY)
        {
            // make memory request:
            MEMORY_MODULE::request_type req{inst->inst_number, c.get(), q.memloc_info.client_id, q.memloc_info.qubit_id};

            // find memory module containing qubit:
            auto m_it = find_memory_module_containing_qubit(q.memloc_info.client_id, q.memloc_info.qubit_id);
            (*m_it)->request_buffer_.push_back(req);

#if defined(QS_SIM_DEBUG)
            if (cycle_ % QS_SIM_DEBUG_FREQ == 0)
            {
                std::cout << "\t\tmemory request for instruction " << *inst
                            << "\tclient = " << q.memloc_info.client_id+0 
                            << ", qubit = " << q.memloc_info.qubit_id << "\n";
            }
#endif

            // set `t_until_in_compute` and `t_free` 
            // to `std::numeric_limits<uint64_t>::max()` to indicate that
            // this is blocked:
            q.memloc_info.t_free = std::numeric_limits<uint64_t>::max();

            result = EXEC_RESULT_MEMORY_STALL;
        }
    }

    // if any qubit is unavailable we need to exit:
    if (result != EXEC_RESULT_SUCCESS)
    {
        // if the gate requires a resource state, then we are also interested if we would have stalled regardless
        if (inst->type == INSTRUCTION::TYPE::T 
                || inst->type == INSTRUCTION::TYPE::TDG
                || inst->type == INSTRUCTION::TYPE::TX
                || inst->type == INSTRUCTION::TYPE::TXDG)
        {
            bool resource_avail = std::any_of(t_fact_.begin(), t_fact_.end(), 
                                    [lvl=target_t_fact_level_] (auto* f) { return f->level >= lvl && f->buffer_occu > 0; });
            if (!resource_avail)
                result |= EXEC_RESULT_RESOURCE_STALL;
        }
        return result;
    }

#if defined(QS_SIM_DEBUG)
    if (cycle_ % QS_SIM_DEBUG_FREQ == 0)
        std::cout << "\t\tall qubits are available -- trying to execute instruction: " << (*inst) << "\n";
#endif

    // if this is a gate that requires a resource, make sure the resource is available:
    switch (inst->type)
    {
    case INSTRUCTION::TYPE::H:
    case INSTRUCTION::TYPE::S:
    case INSTRUCTION::TYPE::SDG:
    case INSTRUCTION::TYPE::SX:
    case INSTRUCTION::TYPE::SXDG:
        result = handle_h_s_gate(c, inst);
        break;
    case INSTRUCTION::TYPE::CX:
    case INSTRUCTION::TYPE::CZ:
        result = handle_cxz_gate(c, inst);
        break;
    case INSTRUCTION::TYPE::RX:
    case INSTRUCTION::TYPE::RZ:
        result = handle_rxz_gate(c, inst);
        break;
    case INSTRUCTION::TYPE::CCX:
    case INSTRUCTION::TYPE::CCZ:
        result = handle_ccxz_gate(c, inst);
        break;
    case INSTRUCTION::TYPE::MZ:
    case INSTRUCTION::TYPE::MX:
        inst->cycle_done = cycle_ + 1;
        inst->is_running = true;
        result = EXEC_RESULT_SUCCESS;
        break;
    default:
        throw std::runtime_error("unsupported instruction: " + inst->to_string());
    }
        
    return result;
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

COMPUTE::exec_result_type
COMPUTE::handle_sw_instruction(client_ptr& c, inst_ptr inst)
{
    if (inst->type == INSTRUCTION::TYPE::SWAP)
    {
        auto& rename_table = renamed_qubits_[c->id];
        // rename the arguments:
        qubit_type q1 = inst->qubits[0],
                   q2 = inst->qubits[1];
        
        // if the qubits are not in the rename table, add them -- this makes the code easier to read:
        if (!rename_table.count(q1))
            rename_table[q1] = q1;
        if (!rename_table.count(q2))
            rename_table[q2] = q2;

        // swap the values in the table;
        std::swap(rename_table[q1], rename_table[q2]);
    }

    inst->cycle_done = cycle_ + 1;
    inst->is_running = true;
    return EXEC_RESULT_SUCCESS;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE::exec_result_type
COMPUTE::handle_h_s_gate(client_ptr& c, inst_ptr inst)
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
    if (cycle_ % QS_SIM_DEBUG_FREQ == 0)
    {
        std::cout << "\t\tbuses near qubit " << inst->qubits[0] << " (patch = " << q_patch.client_id << ", " << q_patch.qubit_id << "):";
        for (auto& b : q_patch.buses)
            std::cout << " " << b->t_free;
        std::cout << "\n";
    }
#endif

    auto it = find_free_bus(q_patch);
    if (it == q_patch.buses.end())
    {
        return EXEC_RESULT_ROUTING_STALL;
    }
    else
    {
        q.memloc_info.t_free = cycle_ + 2;
        (*it)->t_free = cycle_ + 2;
        inst->cycle_done = cycle_ + 2;
        inst->is_running = true;
        return EXEC_RESULT_SUCCESS;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE::exec_result_type
COMPUTE::handle_cxz_gate(client_ptr& c, inst_ptr inst)
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
        return EXEC_RESULT_SUCCESS;
    }
    else
    {
        return EXEC_RESULT_ROUTING_STALL;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE::exec_result_type
COMPUTE::handle_t_gate(client_ptr& c, inst_ptr inst)
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
    exec_result_type result{};
    bool any_factory_has_resource{false};
    for (size_t i = 0; i < t_fact_.size() && !inst->is_running; i++)
    {
        auto* fact = t_fact_[i];
        if (fact->level != target_t_fact_level_ || fact->buffer_occu == 0)
            continue;

        any_factory_has_resource = true;
        // get the factory's output patch and consume the magic state:
        PATCH& f_patch = patches_[fact->output_patch_idx];

        if (alloc_routing_space(f_patch, q_patch, endpoint_latency, path_latency))
        {
            inst->cycle_done = cycle_ + 2 + 2*static_cast<int>(clifford_correction);
            inst->is_running = true;

            q.memloc_info.t_free = cycle_ + 2 + 2*static_cast<int>(clifford_correction);

            fact->buffer_occu--;

            result = EXEC_RESULT_SUCCESS;
        }
        else
        {
            result = EXEC_RESULT_ROUTING_STALL;
        }
    }

    if (!any_factory_has_resource)
        result |= EXEC_RESULT_RESOURCE_STALL;

    return result;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE::exec_result_type
COMPUTE::handle_rxz_gate(client_ptr& c, inst_ptr inst) 
{
    // create uop and run it:
    if (inst->curr_uop == nullptr)
    {
        size_t uop_idx = inst->uop_completed;
        inst->curr_uop = new INSTRUCTION(inst->urotseq[uop_idx], inst->qubits);
    }
    auto result = execute_instruction(c, inst->curr_uop);
    inst->is_running = (result == EXEC_RESULT_SUCCESS);
    return result;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE::exec_result_type
COMPUTE::handle_ccxz_gate(client_ptr& c, inst_ptr inst)
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

    auto result = execute_instruction(c, inst->curr_uop);
    inst->is_running = (result == EXEC_RESULT_SUCCESS);
    return result;
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