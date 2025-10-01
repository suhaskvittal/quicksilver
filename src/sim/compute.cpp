/*
    author: Suhas Vittal
    date:   16 September 2025
*/
#include "sim/clock.h"
#include "sim/compute.h"
#include "sim/cmp/replacement/lru.h"
#include "sim/cmp/replacement/lti.h"

#include "sim.h"

#include <limits>
#include <unordered_set>

//#define COMPUTE_VERBOSE
//#define DISABLE_MEMORY_ROUTING_STALL

namespace sim
{

constexpr size_t NUM_CCZ_UOPS = 13;
constexpr size_t NUM_CCX_UOPS = NUM_CCZ_UOPS+2;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
_get_max_t_factory_level(std::vector<T_FACTORY*> t_fact)
{
    auto max_it = std::max_element(t_fact.begin(), t_fact.end(),
                            [] (const T_FACTORY* a, const T_FACTORY* b) { return a->level_ < b->level_; });
    return (*max_it)->level_;
}

COMPUTE::COMPUTE(double freq_khz, 
                std::vector<std::string> client_trace_files,
                size_t num_rows, 
                size_t num_patches_per_row,
                std::vector<T_FACTORY*> t_fact,
                std::vector<MEMORY_MODULE*> mem_modules,
                REPLACEMENT_POLICY repl_policy)
    :OPERABLE(freq_khz),
    target_t_fact_level_(_get_max_t_factory_level(t_fact)),
    num_rows_(num_rows),
    num_patches_per_row_(num_patches_per_row),
    rename_tables_(client_trace_files.size()),
    t_fact_(t_fact),
    mem_modules_(mem_modules)
{
    // initialize replacement policy:
    switch (repl_policy)
    {
    case REPLACEMENT_POLICY::LTI:
        repl_ = std::make_unique<cmp::repl::LTI>(this);
        break;
    case REPLACEMENT_POLICY::LRU:
        repl_ = std::make_unique<cmp::repl::LRU>(this);
        break;
    default:
        throw std::runtime_error("invalid replacement policy");
    }

    // initialize routing space:
    auto routing_elements = con_init_routing_space();
    // initialize patches:
    con_init_patches(routing_elements);
    // finally initialize clients
    con_init_clients(client_trace_files);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE::memory_route_result_type
COMPUTE::route_memory_access(
    size_t mem_patch_idx,
    QUBIT incoming_qubit,
    bool is_prefetch,
    std::optional<QUBIT> victim,
    uint64_t extra_mem_access_latency_post_routing_ns)
{
    if (!victim.has_value())
    {
        victim = repl_->select_victim(incoming_qubit, is_prefetch);
        if (!victim.has_value())
            return {false, QUBIT{-1,-1}, 0};
    }

    auto v_patch_it = find_patch_containing_qubit(*victim);
    if (v_patch_it == patches_.end())
        throw std::runtime_error("victim qubit " + victim->to_string() + " not found in compute patches");

    PATCH& v_patch = *v_patch_it;
    PATCH& m_patch = patches_[mem_patch_idx];

    auto v_bus_it = find_next_available_bus(v_patch);
    auto m_bus_it = find_next_available_bus(m_patch);

    uint64_t cycle_routing_start = std::max(v_bus_it->cycle_free, m_bus_it->cycle_free);
    cycle_routing_start = std::max(cycle_routing_start, qubit_available_cycle_[incoming_qubit]);
    cycle_routing_start = std::max(cycle_routing_start, qubit_available_cycle_[*victim]);
    cycle_routing_start = std::max(cycle_routing_start, current_cycle());

#if defined(DISABLE_MEMORY_ROUTING_STALL)
    uint64_t routing_alloc_cycle = cycle_routing_start;
#else
    auto [path, routing_alloc_cycle] = route_path_from_src_to_dst(v_bus_it, m_bus_it, cycle_routing_start);
    update_free_times_along_routing_path(path, routing_alloc_cycle, routing_alloc_cycle);
#endif

    // update operands' available cycles
    uint64_t extra_mem_access_cycles = convert_ns_to_cycles(extra_mem_access_latency_post_routing_ns, OP_freq_khz);
    qubit_available_cycle_[*victim] = routing_alloc_cycle + extra_mem_access_cycles;
    qubit_available_cycle_[incoming_qubit] = routing_alloc_cycle + extra_mem_access_cycles;

    // need to track this if simulator-directed memory accesses are disabled (accesses done by MSWAP and MPREFETCH instead)
    if (GL_DISABLE_SIMULATOR_DIRECTED_MEMORY_ACCESS)
    {
        qubits_unavailable_to_due_memory_access_.insert(incoming_qubit);
        qubits_unavailable_to_due_memory_access_.insert(*victim);
    }

    s_evictions_no_uses_ += (v_patch.num_uses == 0);
    s_evictions_prefetch_no_uses_ += is_prefetch && (v_patch.num_uses == 0);

    // set `v_patch` contents to `incoming_qubit`
    v_patch.is_prefetched = is_prefetch;
    v_patch.num_uses = 0;
    v_patch.contents = incoming_qubit;

    // update replacement policy:
    repl_->update_on_fill(incoming_qubit);

    uint64_t access_time_ns = convert_cycles_to_ns(routing_alloc_cycle - current_cycle(), OP_freq_khz);
    return {true, *victim, access_time_ns};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE::OP_init()
{
    // start all clients in init
    for (auto& c : clients_)
    {
        client_fetch(c);
        client_schedule(c);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE::dump_deadlock_info()
{
    std::cerr << "=========COMPUTE DEADLOCK INFO================\n";
    std::cerr << "current cycle = " << current_cycle() << "\n";

    std::cerr << "compute memory contents:\n";
    for (size_t i = compute_start_idx_; i < memory_start_idx_; i++)
    {
        std::cerr << "\tPATCH " << (i-compute_start_idx_) << ", contents = " << patches_[i].contents << ", num uses = " << patches_[i].num_uses << "\n";
    }

    for (auto& c : clients_)
    {
        std::cerr << "CLIENT " << static_cast<int>(c->id) << ":\n";
        for (qubit_type qid = 0; qid < static_cast<qubit_type>(c->num_qubits); qid++)
        {
            QUBIT q{c->id, qid};
            std::cerr << "\tQUBIT " << qid << ", cycle avail = " << qubit_available_cycle_[q] << ", instruction window:\n";
            for (auto* inst : inst_windows_[q])
            {
                bool is_sw = is_software_instruction(inst->type);
                std::vector<bool> ready(inst->qubits.size(), false);
                std::transform(inst->qubits.begin(), inst->qubits.end(), ready.begin(),
                                [this, &c, inst, is_sw] (qubit_type qid) 
                                {
                                    QUBIT q{c->id, qid};
                                    const auto& w = this->inst_windows_[q];
                                    bool at_head = (!w.empty() && w.front() == inst);
                                    return at_head;
                                });
                if (!inst->is_scheduled && !std::all_of(ready.begin(), ready.end(), [](bool r) { return r; }))
                    continue;
            
                bool has_memory_stall = std::find_if(inst_waiting_for_memory_.begin(), inst_waiting_for_memory_.end(), [inst](const auto& p) { return p.first == inst; }) != inst_waiting_for_memory_.end();
                bool has_resource_stall = std::find_if(inst_waiting_for_resource_.begin(), inst_waiting_for_resource_.end(), [inst](const auto& p) { return p.first == inst; }) != inst_waiting_for_resource_.end();

                std::cerr << "\t\t" << *inst << ", uop " << inst->uop_completed << "/" << inst->num_uops 
                        << ", scheduled = " << inst->is_scheduled << ", ready = ";
                for (bool r : ready)
                    std::cerr << r << " ";
                std::cerr << ", cycle done = " << inst->cycle_done 
                        << ", has memory stall = " << has_memory_stall 
                        << ", has resource stall = " << has_resource_stall << "\n";
            }
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE::OP_handle_event(event_type event)
{
    if (event.id == COMPUTE_EVENT_TYPE::MAGIC_STATE_AVAIL)
    {
#if defined(COMPUTE_VERBOSE)
        std::cout << "[ COMPUTE ] got event: magic state available @ cycle = " << current_cycle() << "\n";
#endif
        retry_instructions(RETRY_TYPE::RESOURCE, event.info);
    }
    else if (event.id == COMPUTE_EVENT_TYPE::MEMORY_ACCESS_DONE)
    {
#if defined(COMPUTE_VERBOSE)
        std::cout << "[ COMPUTE ] got event: memory access done @ cycle = " << current_cycle() 
                << " requested = " << event.info.mem_accessed_qubit << ")"
                << " victim = " << event.info.mem_victim_qubit << "\n";
#endif
        retry_instructions(RETRY_TYPE::MEMORY, event.info);
    }
    else if (event.id == COMPUTE_EVENT_TYPE::INST_EXECUTE)
    {
#if defined(COMPUTE_VERBOSE)
        std::cout << "[ COMPUTE ] got event: instruction execute @ cycle = " << current_cycle() 
                    << ", instruction \"" << *event.info.inst << "\"\n";
#endif
        client_execute(clients_[event.info.client_id], event.info.inst);
    }
    else if (event.id == COMPUTE_EVENT_TYPE::INST_COMPLETE)
    {
#if defined(COMPUTE_VERBOSE)
        std::cout << "[ COMPUTE ] got event: instruction complete @ cycle = " << current_cycle() 
                    << ", instruction \"" << *event.info.inst << "\"\n";
#endif
        auto& c = clients_[event.info.client_id];
        client_retire(c, event.info.inst);
        client_fetch(c);
        client_schedule(c);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
_connect(ROUTING_COMPONENT::ptr_type src, ROUTING_COMPONENT::ptr_type dst)
{
    src->connections.push_back(dst);
    dst->connections.push_back(src);
}

COMPUTE::routing_info
COMPUTE::con_init_routing_space()
{
    // buses and junctions are arranged by pairs of rows:
    const size_t num_row_pairs = (num_rows_+1)/2;  // need to +1 to handle singleton row

    const size_t num_junctions = 2*num_row_pairs + 2;
    const size_t num_buses = 3*num_row_pairs + 1;

    std::vector<ROUTING_COMPONENT::ptr_type> junctions(num_junctions);
    std::vector<ROUTING_COMPONENT::ptr_type> buses(num_buses);

    for (size_t i = 0; i < junctions.size(); i++)
        junctions[i] = ROUTING_COMPONENT::ptr_type(new ROUTING_COMPONENT);
    
    for (size_t i = 0; i < buses.size(); i++)
        buses[i] = ROUTING_COMPONENT::ptr_type(new ROUTING_COMPONENT);

    for (size_t i = 0; i < num_row_pairs; i++)
    {
        // 2i ----- 3i ------ 2i+1
        //  |                  |
        // 3i+1               3i+2
        //  |                  |
        // 2i+2 --- 3i+3 ----- 2i+3
        _connect(junctions[2*i], buses[3*i]);
        _connect(junctions[2*i+1], buses[3*i]);

        _connect(junctions[2*i], buses[3*i+1]);
        _connect(junctions[2*i+2], buses[3*i+1]);

        _connect(junctions[2*i+1], buses[3*i+2]);
        _connect(junctions[2*i+3], buses[3*i+2]);
    }

    // connect last bus
    _connect(buses[num_buses-1], junctions[num_junctions-2]);
    _connect(buses[num_buses-1], junctions[num_junctions-1]);

    return routing_info{junctions, buses};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE::con_init_patches(routing_info routing_elements)
{
    auto [junctions, buses] = routing_elements;

    std::vector<T_FACTORY*> top_level_t_fact;
    std::copy_if(t_fact_.begin(), t_fact_.end(), std::back_inserter(top_level_t_fact),
                [lvl=target_t_fact_level_] (T_FACTORY* f) { return f->level_ == lvl; });

    const size_t full_row_width_inc_ancilla = num_patches_per_row_ + 2;

    const size_t num_factory_pins = std::min(top_level_t_fact.size(), full_row_width_inc_ancilla);
    const size_t num_memory_pins = std::min(mem_modules_.size(), full_row_width_inc_ancilla);
    const size_t total_patches = num_rows_*num_patches_per_row_ + num_factory_pins + num_memory_pins;

    patches_.resize(total_patches);

    // determine the following:
    compute_start_idx_ = num_factory_pins;
    memory_start_idx_ = compute_start_idx_ + num_rows_*num_patches_per_row_;

    // now connect the magic state factories:
    for (size_t i = 0; i < top_level_t_fact.size(); i++)
    {
        T_FACTORY* f = top_level_t_fact[i];
        size_t i_mod = i % full_row_width_inc_ancilla;
        f->output_patch_idx_ = i_mod;

        PATCH& fp = patches_[f->output_patch_idx_];
        if (i == i_mod)
        {
            if (i_mod == 0)
                fp.buses.push_back(junctions[0]);
            else if (i_mod == full_row_width_inc_ancilla-1)
                fp.buses.push_back(junctions[1]);
            else
                fp.buses.push_back(buses[0]);
        }
    }

    // now connect the program memory patches:
    for (size_t p = compute_start_idx_; p < memory_start_idx_; p++)
    {
        // get row idx and column idx:
        size_t r = (p-compute_start_idx_) / num_patches_per_row_;
        size_t c = (p-compute_start_idx_) % num_patches_per_row_;

        // compute row pair idx:
        size_t rp = r/2;

        bool is_upper = (r & 1) == 0;  // even rows are always upper
        bool is_lower = ((r & 1) == 1) || (r == num_rows_-1);  // odd rows are always lower, and the last row is always lower
        bool is_left = (c == 0);
        bool is_right = (c == num_patches_per_row_-1);

        if (is_upper)
            patches_[p].buses.push_back(buses[3*rp]);
        if (is_left)
            patches_[p].buses.push_back(buses[3*rp+1]);
        if (is_lower)
            patches_[p].buses.push_back(buses[3*rp+2]);
        if (is_right)
            patches_[p].buses.push_back(buses[3*rp+3]);
    }

    // set the connections for the memory pins -- do same interleaving as for factories
    const auto& last_bus = buses.back();
    const auto& penult_junction = junctions[junctions.size()-2];
    const auto& last_junction = junctions.back();
    for (size_t i = 0; i < mem_modules_.size(); i++)
    {
        auto* m = mem_modules_[i];
        size_t i_mod = i % full_row_width_inc_ancilla;
        m->output_patch_idx_ = i_mod + compute_start_idx_;

        PATCH& mp = patches_[m->output_patch_idx_];
        if (i == i_mod)
        {
            if (i_mod == 0)
                mp.buses.push_back(penult_junction);
            else if (i_mod == full_row_width_inc_ancilla-1)
                mp.buses.push_back(last_junction);
            else
                mp.buses.push_back(last_bus);
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE::con_init_clients(std::vector<std::string> client_trace_files)
{
    // initialize all clients:
    for (size_t i = 0; i < client_trace_files.size(); i++)
        clients_.push_back(std::make_unique<CLIENT>(client_trace_files[i], i));

    
    // place qubits into compute memory (round robin fashion to be fair)
    QUBIT curr_qubit{0,0};

    // place qubits into memory once `patch_idx` reaches `patch_idx_memory_start_`:
    // do this in a round robin fashion to maximize module level and bank level parallelism
    std::vector<QUBIT> qubits_to_place_in_mem;

    size_t p{compute_start_idx_};
    std::vector<bool> clients_done(clients_.size(), false);
    bool all_done{false};
    while (!all_done)
    {
        const auto& c = clients_[curr_qubit.client_id];
        if (curr_qubit.qubit_id >= c->num_qubits)
        {
            if (!clients_done[curr_qubit.client_id])
            {
                clients_done[curr_qubit.client_id] = true;
                all_done = std::all_of(clients_done.begin(), clients_done.end(), [](bool b) { return b; });
            }
        }
        else if (p >= memory_start_idx_)
        {
            qubits_to_place_in_mem.push_back(curr_qubit);
        }
        else
        {
            patches_[p].contents = curr_qubit;
            p++;
        }

        curr_qubit.client_id++;
        if (curr_qubit.client_id >= clients_.size())
        {
            curr_qubit.client_id = 0;
            curr_qubit.qubit_id++;
        }
    }

    mem_alloc_qubits_in_round_robin(mem_modules_, qubits_to_place_in_mem);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE::client_fetch(client_ptr& c)
{
    constexpr size_t INST_READ_LIMIT{8192};

    uint64_t total_inflight_instructions = std::transform_reduce(inst_windows_.begin(), inst_windows_.end(), 0, std::plus<>(),
                                                                [](const auto& pair) { return pair.second.size(); });
    if (total_inflight_instructions >= 2*INST_READ_LIMIT)
        return;

    // find a qubit with an empty window:
    std::vector<QUBIT> qubits;
    for (qubit_type qid = 0; qid < static_cast<qubit_type>(c->num_qubits); qid++)
        qubits.push_back(QUBIT{c->id, qid});
    auto q_it = std::find_if(qubits.begin(), qubits.end(),
                            [this] (const auto& q) { return this->inst_windows_[q].empty(); });
    
    size_t num_read{0}; 
    while (q_it != qubits.end() && num_read < INST_READ_LIMIT)
    {
        bool found_target_qubit{false};
        while (num_read < INST_READ_LIMIT && !found_target_qubit)
        {
            // read next instruction:
            inst_ptr inst = c->read_instruction_from_trace();
            if (inst->type == INSTRUCTION::TYPE::NIL)
                continue;
            num_read++;

            // handle any renaming:
            /*
            if (inst->type != INSTRUCTION::TYPE::SWAP)
            {
                for (qubit_type qid : inst->qubits)
                    qid = rename_tables_[c->id].count(qid) ? rename_tables_[c->id].at(qid) : qid;
            }
            */

            // set the number of uops (may depend on simulator config)
            if (inst->type == INSTRUCTION::TYPE::RX || inst->type == INSTRUCTION::TYPE::RZ)
                inst->num_uops = inst->urotseq.size();
            else if (inst->type == INSTRUCTION::TYPE::CCX || inst->type == INSTRUCTION::TYPE::CCZ)
                inst->num_uops = inst->type == INSTRUCTION::TYPE::CCX ? NUM_CCX_UOPS : NUM_CCZ_UOPS;
            else
                inst->num_uops = 0;

            // add the instruction to the windows of all the qubits it operates on
            for (qubit_type qid : inst->qubits)
            {
                QUBIT q{c->id, qid};
                inst_windows_[q].push_back(inst);
                found_target_qubit |= (q == *q_it);
            }
        }

        q_it = std::find_if(qubits.begin(), qubits.end(),
                                [this] (const auto& q) { return this->inst_windows_[q].empty(); });
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE::client_schedule(client_ptr& c)
{
    // note that instructions may appear in different windows, so we need to keep track of visited instructions
    std::unordered_set<inst_ptr> visited;

    for (qubit_type qid = 0; qid < static_cast<qubit_type>(c->num_qubits); qid++)
    {
        QUBIT q{c->id, qid};
        const auto& win = inst_windows_[q];
        if (win.empty())
            continue;

        inst_ptr inst = win.front();
        if (visited.count(inst) || inst->is_scheduled)
            continue;
        visited.insert(inst);

        // verify two things:
        //   1. the instruction is at the head of qubits' windows
        //   2. all qubits can actually operate at this time (if this is not a software instruction)
        bool all_ready = std::all_of(inst->qubits.begin(), inst->qubits.end(),
                                        [this, client_id=c->id, inst] (qubit_type qid) 
                                        {
                                            QUBIT q{client_id, qid};
                                            const auto& w = this->inst_windows_[q];
                                            bool at_head = (!w.empty() && w.front() == inst);
                                            return at_head;
                                        });
        if (!all_ready)
            continue;

        inst->is_scheduled = true;

#if defined(COMPUTE_VERBOSE)
        std::cout << "\tclient " << static_cast<int>(c->id) 
                    << " instruction \"" << *inst << "\" is ready @ cycle = " << current_cycle() << "\n";
#endif

        // schedule the instruction execution:
        std::vector<uint64_t> avail_cycles;
        std::transform(inst->qubits.begin(), inst->qubits.end(), std::back_inserter(avail_cycles),
                        [this, client_id=c->id] (qubit_type qid) 
                        {
                            QUBIT q{client_id, qid};
                            return this->qubit_available_cycle_[q];
                        });
        auto max_it = std::max_element(avail_cycles.begin(), avail_cycles.end());
        if (*max_it > current_cycle())
        {
            OP_add_event_using_cycles(COMPUTE_EVENT_TYPE::INST_EXECUTE, *max_it - current_cycle(), COMPUTE_EVENT_INFO{c->id, inst});

            if (GL_DISABLE_SIMULATOR_DIRECTED_MEMORY_ACCESS)
            {
                // we want to count the time between the latest memory stall to the latest non-memory stall
                // -- the isolated stall time is the difference between these two times
                QUBIT latest_mem_stall_q{-1, -1};
                QUBIT latest_no_mem_stall_q{-1, -1};

                for (qubit_type qid : inst->qubits)
                {
                    QUBIT q{c->id, qid};
                    uint64_t cycle_avail = qubit_available_cycle_[q];
                    if (cycle_avail < current_cycle())
                        continue;

                    if (qubits_unavailable_to_due_memory_access_.count(q))
                    {
                        if (latest_mem_stall_q.qubit_id < 0 || cycle_avail > qubit_available_cycle_[latest_mem_stall_q])
                            latest_mem_stall_q = q;
                    }
                    else
                    {
                        if (latest_no_mem_stall_q.qubit_id < 0 || cycle_avail > qubit_available_cycle_[latest_no_mem_stall_q])
                            latest_no_mem_stall_q = q;
                    }
                }

                if (latest_mem_stall_q.qubit_id >= 0 && latest_no_mem_stall_q.qubit_id >= 0)
                {
                    uint64_t mem_stall_end = qubit_available_cycle_[latest_mem_stall_q];
                    uint64_t non_mem_stall_end = qubit_available_cycle_[latest_no_mem_stall_q];

                    if (mem_stall_end > non_mem_stall_end)
                        inst->total_isolated_memory_stall_cycles += mem_stall_end - non_mem_stall_end;
                }
                else if (latest_mem_stall_q.qubit_id >= 0)
                {
                    inst->total_isolated_memory_stall_cycles += qubit_available_cycle_[latest_mem_stall_q] - current_cycle();
                }
                // don't care about other cases -- these have no memory stalls
            }
        }
        else // save a little time and execute right now:
        {
            client_execute(c, inst);
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE::client_execute(client_ptr& c, inst_ptr inst)
{
    // check if this is an MSWAP instruction -- cannot fail
    exec_result_type result;
    if (inst->type == INSTRUCTION::TYPE::MSWAP || inst->type == INSTRUCTION::TYPE::MPREFETCH)
        result = do_mswap_or_mprefetch(c, inst);
    else
        result = execute_instruction(c, inst);
    process_execution_result(c, inst, result);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE::client_retire(client_ptr& c, inst_ptr inst)
{
#if defined(COMPUTE_VERBOSE)
    std::cout << "\tinstruction \"" << *inst << "\" is being completed @ cycle = " << current_cycle()
            << ", uop " << inst->uop_completed << " of " << inst->num_uops << "\n";
#endif

    bool all_done{true};
    if (inst->num_uops > 0)
    {
        // if `num_uops > 0` then `all_done` depends on the number of uops completed
        if (inst->curr_uop != nullptr)
        {
            delete inst->curr_uop;
            inst->curr_uop = nullptr;
            inst->is_scheduled = false;
            inst->uop_completed++;
            all_done = (inst->uop_completed == inst->num_uops);
        }
    }

    if (all_done)
    {
        uint64_t unrolled_inst_done_before = c->s_unrolled_inst_done;

        // do not increment instruction done count for directive-like instructions (i.e., mswap)
        if (inst->type != INSTRUCTION::TYPE::MSWAP && inst->type != INSTRUCTION::TYPE::MPREFETCH)
        {
            c->s_inst_done++;
            if (inst->num_uops > 0)
                c->s_unrolled_inst_done += inst->num_uops;
            else
                c->s_unrolled_inst_done++;
        }
        else if (inst->type == INSTRUCTION::TYPE::MSWAP)
        {
            c->s_mswap_count++;
        }
        else if (inst->type == INSTRUCTION::TYPE::MPREFETCH)
        {
            c->s_mprefetch_count++;
        }

        // remove the instruction from all windows it is in
        for (qubit_type qid : inst->qubits)
        {
            QUBIT q{c->id, qid};
            inst_ptr front_inst = inst_windows_[q].front();
            if (front_inst != inst)
            {
                std::cerr << "instruction window of qubit " << qid << ":\n";
                for (auto* inst : inst_windows_[q])
                    std::cerr << "\t" << *inst << "\n";
                throw std::runtime_error("instruction `" + inst->to_string() 
                                        + "` is not at the head of qubit " 
                                        + std::to_string(qid) + " window");
            }
            inst_windows_[q].pop_front();
        }
        delete inst;

        // print progress if flag is set:
        bool pp = (c->s_unrolled_inst_done % GL_PRINT_PROGRESS_FREQ) 
                    < (unrolled_inst_done_before % GL_PRINT_PROGRESS_FREQ);
        if (GL_PRINT_PROGRESS && (pp || GL_PRINT_PROGRESS_FREQ == 1))
        {
            double t_min = (static_cast<double>(current_cycle()) / OP_freq_khz) * 1e-3 / 60.0;
            double kips = static_cast<double>(c->s_unrolled_inst_done) / (t_min*60) * 1e-3;
            std::cout << "CLIENT " << static_cast<int>(c->id)
                        << " @ " << c->s_unrolled_inst_done << " unrolled instructions done (virtual instructions done = " << c->s_inst_done << ")\n"
                        << "\tcompute cycle = " << current_cycle() << "\n"
                        << "\tsimulated execution time = " << t_min << " minutes\n"
                        << "\tinstruction rate = " << kips << " kiloinstructions/s (KIPS)\n"
                        << "\n";
        }

        // as this instruction is retired, it may be possible to find victims for any pending memory requests
        for (auto* m : mem_modules_)
            m->OP_add_event(MEMORY_EVENT_TYPE::COMPUTE_COMPLETED_INST, 0);
    }
}


////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE::exec_result_type
COMPUTE::execute_instruction(client_ptr& c, inst_ptr inst)
{
    // check if the instruction is a software instruction
    if (is_software_instruction(inst->type))
        return do_sw_gate(c, inst);

    // do memory access if necessary
    exec_result_type result{};

    std::vector<PATCH*> qubit_patches(inst->qubits.size());
    for (size_t i = 0; i < inst->qubits.size(); i++)
    {
        QUBIT q{c->id, inst->qubits[i]};
        auto patch_it = find_patch_containing_qubit(q);
        if (patch_it == patches_.end())
        {
            // this means that the qubit is memory -- we need to do a memory access
            result.is_memory_stall = true;

            // find the module that contains the qubit and initiate the memory access
            if (!GL_DISABLE_SIMULATOR_DIRECTED_MEMORY_ACCESS)
                access_memory_and_die_if_qubit_not_found(inst, q);
        }
        else
        {
            qubit_patches[i] = &(*patch_it);
        }
    }

    // return early if we are waiting for memory
    if (result.is_memory_stall)
    {
        // check if there is also a concurrent resource stall
        bool needs_resource = (inst->type == INSTRUCTION::TYPE::T || inst->type == INSTRUCTION::TYPE::TX
                                || inst->type == INSTRUCTION::TYPE::TDG || inst->type == INSTRUCTION::TYPE::TXDG
                                || inst->type == INSTRUCTION::TYPE::RZ || inst->type == INSTRUCTION::TYPE::RX
                                || inst->type == INSTRUCTION::TYPE::CCX || inst->type == INSTRUCTION::TYPE::CCZ);
        // can also happen if the uop is a t gate:
        if (needs_resource)
        {
            size_t num_resource_states_avail = std::transform_reduce(t_fact_.begin(), t_fact_.end(),
                                                                        size_t{0},
                                                                        std::plus<size_t>{},
                                                                        [this] (T_FACTORY* f) 
                                                                        { 
                                                                            return (f->level_ >= this->target_t_fact_level_ ? f->buffer_occu_ : 0);
                                                                        });
            result.is_resource_stall = (num_resource_states_avail == 0);
        }
        return result;
    }

    if (GL_IMPL_RZ_PREFETCH)
    {
        try_rz_directed_prefetch(c, inst);
    }

    return do_gate(c, inst, qubit_patches);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE::process_execution_result(client_ptr& c, inst_ptr inst, exec_result_type result)
{
    if (result.is_memory_stall)
    {
        inst_waiting_for_memory_.emplace_back(inst, c->id);
        
        // only start tracking if this is an isolated memory stall
        if (!result.is_resource_stall)
            inst->memory_stall_start_cycle = std::min(inst->memory_stall_start_cycle, current_cycle());

#if defined(COMPUTE_VERBOSE)
        std::cout << "\tinstruction \"" << *inst << "\" is waiting for memory access\n";
#endif
    }
    else if (result.is_resource_stall)
    {
        inst_waiting_for_resource_.emplace_back(inst, c->id);
        inst->resource_stall_start_cycle = std::min(inst->resource_stall_start_cycle, current_cycle());

#if defined(COMPUTE_VERBOSE)
        std::cout << "\tinstruction \"" << *inst << "\" is waiting for resource\n";
#endif
    }
    else
    {
#if defined(COMPUTE_VERBOSE)
        std::cout << "\tclient " << static_cast<int>(c->id) 
                    << " instruction \"" << *inst << "\" will complete @ cycle = " 
                    << (current_cycle() + result.cycles_until_done) 
                    << ", latency = " << result.cycles_until_done
                    << "\n";
#endif
        if (result.cycles_until_done > 1'000'000)
        {
            throw std::runtime_error("instruction " + inst->to_string() + " will take too long to complete "
                                    + " -- definitely a bug, latency = " + std::to_string(result.cycles_until_done) + " cycles");
        }

        COMPUTE_EVENT_INFO event_info;
        event_info.client_id = c->id;
        event_info.inst = inst;
        
        OP_add_event_using_cycles(COMPUTE_EVENT_TYPE::INST_COMPLETE, result.cycles_until_done, event_info);

        // set the qubit availability for all qubits in the instruction
        // also delete the instruction from the windows of all qubits
        for (qubit_type qid : inst->qubits)
        {
            QUBIT q{c->id, qid};
            qubit_available_cycle_[q] = current_cycle() + result.cycles_until_done;

            if (inst->type != INSTRUCTION::TYPE::MSWAP && inst->type != INSTRUCTION::TYPE::MPREFETCH)
            {
                if (GL_DISABLE_SIMULATOR_DIRECTED_MEMORY_ACCESS && qubits_unavailable_to_due_memory_access_.count(q))
                    qubits_unavailable_to_due_memory_access_.erase(q);
            }
        }

        // update stall related stats:
        uint64_t routing_stall_cycles = result.routing_stall_cycles;
        uint64_t memory_stall_cycles = inst->total_isolated_memory_stall_cycles;
        uint64_t resource_stall_cycles = inst->total_isolated_resource_stall_cycles;

        inst->cycle_done = current_cycle() + result.cycles_until_done;

        c->s_inst_routing_stall_cycles += routing_stall_cycles;
        c->s_inst_memory_stall_cycles += memory_stall_cycles;
        c->s_inst_resource_stall_cycles += resource_stall_cycles;

        // reset the stats for the instruction:
        inst->total_isolated_memory_stall_cycles = 0;
        inst->total_isolated_resource_stall_cycles = 0;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE::exec_result_type
COMPUTE::do_gate(client_ptr& c, inst_ptr inst, std::vector<PATCH*> qubit_patches)
{
    exec_result_type result{};

    switch (inst->type)
    {
    case INSTRUCTION::TYPE::X:
    case INSTRUCTION::TYPE::Y:
    case INSTRUCTION::TYPE::Z:
    case INSTRUCTION::TYPE::SWAP:
        result = do_sw_gate(c, inst);
        break;

    case INSTRUCTION::TYPE::H:
    case INSTRUCTION::TYPE::S:
    case INSTRUCTION::TYPE::SDG:
    case INSTRUCTION::TYPE::SX:
    case INSTRUCTION::TYPE::SXDG:
        result = do_h_or_s_gate(c, inst, qubit_patches);
        break;

    case INSTRUCTION::TYPE::T:
    case INSTRUCTION::TYPE::TX:
    case INSTRUCTION::TYPE::TDG:
    case INSTRUCTION::TYPE::TXDG:
        result = do_t_gate(c, inst, qubit_patches);
        break;

    case INSTRUCTION::TYPE::CX:
    case INSTRUCTION::TYPE::CZ:
        result = do_cx_gate(c, inst, qubit_patches);
        break;

    case INSTRUCTION::TYPE::RZ:
    case INSTRUCTION::TYPE::RX:
        result = do_rz_gate(c, inst, qubit_patches);
        break;

    case INSTRUCTION::TYPE::CCX:
    case INSTRUCTION::TYPE::CCZ:
        result = do_ccx_gate(c, inst, qubit_patches);
        break;

    default:
        throw std::runtime_error("invalid instruction type: " + std::string{BASIS_GATES[static_cast<size_t>(inst->type)]});
    }

    return result;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE::exec_result_type
COMPUTE::do_sw_gate(client_ptr& c, inst_ptr inst)
{
    exec_result_type result{};

    /*
    if (inst->type == INSTRUCTION::TYPE::SWAP)
    {
        // update qubit renaming table and then exit
        auto& rename_table = rename_tables_[c->id];
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
    */

    // exit
    result.cycles_until_done = 0;
    return result;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE::exec_result_type
COMPUTE::do_h_or_s_gate(client_ptr& c, inst_ptr inst, std::vector<PATCH*> qubit_patches)
{
    exec_result_type result{};

    // both gates requires 2 cycles to complete + routing space
    PATCH& q_patch = *qubit_patches[0];
    auto bus_it = find_next_available_bus(q_patch);
    
    // get earliest start time for gate:
    uint64_t cycle_routing_start = std::max(bus_it->cycle_free, current_cycle());
    cycle_routing_start = std::max(cycle_routing_start, qubit_available_cycle_[q_patch.contents]);
    
    // compute amount of time spent in routing stall:
    uint64_t latency = (cycle_routing_start - current_cycle()) + 2;
    bus_it->cycle_free = current_cycle() + latency;
    
    // only 2 cycles are spent actually doing the gate -- any other latency is due to routing
    result.routing_stall_cycles = latency - 2;
    result.cycles_until_done = latency;

    q_patch.num_uses++;

    return result;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE::exec_result_type
COMPUTE::do_t_gate(client_ptr& c, inst_ptr inst, std::vector<PATCH*> qubit_patches)
{
    exec_result_type result{};

    // find a factory with a resource state
    std::vector<T_FACTORY*> producers;
    std::copy_if(t_fact_.begin(), t_fact_.end(), std::back_inserter(producers),
                [this] (T_FACTORY* f) { return f->level_ >= this->target_t_fact_level_ && f->buffer_occu_ > 0; });
    if (producers.empty())
    {
        result.is_resource_stall = true;
    }
    else
    {
        bool clifford_correction = (GL_RNG() & 1) > 0;
        uint64_t latency = clifford_correction ? 4 : 2;

        PATCH& q_patch = *qubit_patches[0];
        auto q_bus_it = find_next_available_bus(q_patch);

        for (auto* f : producers)
        {
            // TODO: check if routing space is available
            PATCH& f_patch = patches_[f->output_patch_idx_];
            auto f_bus_it = find_next_available_bus(f_patch);

            // three way max to determine when to start routing
            uint64_t cycle_routing_start = std::max(std::max(q_bus_it->cycle_free, f_bus_it->cycle_free), current_cycle());
            cycle_routing_start = std::max(cycle_routing_start, qubit_available_cycle_[q_patch.contents]);

            auto [path, routing_alloc_cycle] = route_path_from_src_to_dst(q_bus_it, f_bus_it, cycle_routing_start);
            update_free_times_along_routing_path(path, routing_alloc_cycle+2, routing_alloc_cycle+latency);

            // note that `routing_alloc_cycle - current_cycle()` is number of cycles spent waiting for routing space
            f->consume_state();
            result.routing_stall_cycles = routing_alloc_cycle - current_cycle();
            result.cycles_until_done = latency + result.routing_stall_cycles;

            // update t gate count and total t error
            c->s_t_gate_count++;
            c->s_total_t_error += f->output_error_prob_;
            
            q_patch.num_uses++;
            return result;
        }
    }

    return result;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE::exec_result_type
COMPUTE::do_cx_gate(client_ptr& c, inst_ptr inst, std::vector<PATCH*> qubit_patches)
{
    exec_result_type result{};

    PATCH& q0_patch = *qubit_patches[0];
    PATCH& q1_patch = *qubit_patches[1];
    auto q0_bus_it = find_next_available_bus(q0_patch);
    auto q1_bus_it = find_next_available_bus(q1_patch);

    uint64_t cycle_routing_start = std::max(std::max(q0_bus_it->cycle_free, q1_bus_it->cycle_free), current_cycle());
    cycle_routing_start = std::max(cycle_routing_start, qubit_available_cycle_[q0_patch.contents]);
    cycle_routing_start = std::max(cycle_routing_start, qubit_available_cycle_[q1_patch.contents]);

    auto [path, routing_alloc_cycle] = route_path_from_src_to_dst(q0_bus_it, q1_bus_it, cycle_routing_start);
    update_free_times_along_routing_path(path, routing_alloc_cycle+2, routing_alloc_cycle+2);

    result.routing_stall_cycles = routing_alloc_cycle - current_cycle();
    result.cycles_until_done = 2 + result.routing_stall_cycles;

    q0_patch.num_uses++;
    q1_patch.num_uses++;

    return result;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE::exec_result_type
COMPUTE::do_rz_gate(client_ptr& c, inst_ptr inst, std::vector<PATCH*> qubit_patches)
{
    if (inst->curr_uop == nullptr)
    {
        size_t uop_idx = inst->uop_completed;
        inst->curr_uop = new INSTRUCTION(inst->urotseq[uop_idx], inst->qubits);
    }
    return do_gate(c, inst->curr_uop, qubit_patches);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE::exec_result_type
COMPUTE::do_ccx_gate(client_ptr& c, inst_ptr inst, std::vector<PATCH*> qubit_patches)
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

    return do_gate(c, inst->curr_uop, qubit_patches);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE::exec_result_type
COMPUTE::do_mswap_or_mprefetch(client_ptr& c, inst_ptr inst)
{
    exec_result_type result{};
    if (GL_ELIDE_MSWAP_INSTRUCTIONS || (inst->type == INSTRUCTION::TYPE::MPREFETCH && GL_ELIDE_MPREFETCH_INSTRUCTIONS))
        return result;

    if (!GL_DISABLE_SIMULATOR_DIRECTED_MEMORY_ACCESS)
        throw std::runtime_error("MSWAP and MPREFETCH instructions should only be added when simulator-directed memory accesses are disabled (add -dsma flag)");

    // check if the operands are in memory
    qubit_type q_requested = inst->qubits[0];
    qubit_type q_victim = inst->qubits[1];

    QUBIT requested{c->id, q_requested};
    QUBIT victim{c->id, q_victim};
    auto module_it = find_memory_module_containing_qubit(requested);
    if (module_it == mem_modules_.end())
    {
        std::cerr << "compute patches:\n";
        for (size_t i = compute_start_idx_; i < memory_start_idx_; i++)
            std::cerr << "\t" << patches_[i].contents << "\n";

        for (size_t i = 0; i < mem_modules_.size(); i++)
        {
            std::cerr << "memory module " << i << "------------------\n";
            mem_modules_[i]->dump_contents();
        }

        throw std::runtime_error("mswap/mprefetch: qubit " + requested.to_string() + " not found in any memory module -- inst: " + inst->to_string());
    }

    // this is a demand access -- must be served immediately
    result.is_memory_stall = !(*module_it)->serve_mswap(inst, requested, victim);
    result.cycles_until_done = 0;
    return result;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE::retry_instructions(RETRY_TYPE type, COMPUTE_EVENT_INFO event_info)
{
    if (type == RETRY_TYPE::MEMORY)
    {
        // find instructions waiting for the completed memory access to finish:
        QUBIT q_accessed = event_info.mem_accessed_qubit;
        QUBIT q_victim = event_info.mem_victim_qubit;
        for (size_t i = 0; i < inst_waiting_for_memory_.size(); i++)
        {
            auto [inst, client_id] = inst_waiting_for_memory_[i];
            bool client_match = client_id == q_accessed.client_id;

            if (inst->type == INSTRUCTION::TYPE::MSWAP || inst->type == INSTRUCTION::TYPE::MPREFETCH)
            {
                // check that both qubits match;
                bool qubit_match = (inst->qubits[0] == q_accessed.qubit_id && inst->qubits[1] == q_victim.qubit_id);
                if (client_match && qubit_match)
                {
                    exec_result_type result{};
                    result.cycles_until_done = 0;
                    process_execution_result(clients_[client_id], inst, result);
                    inst_waiting_for_memory_[i].first = nullptr;  // set to nullptr to indicate that this entry is invalid
                }
                continue;
            }

            bool qubit_match = std::find(inst->qubits.begin(), inst->qubits.end(), q_accessed.qubit_id) != inst->qubits.end();
            if (client_match && qubit_match)
            {

                auto& c = clients_[client_id];
                auto result = execute_instruction(c, inst);
                // if we are not stalled by memory anymore, then we can remove the instruction from the buffer
                if (!result.is_memory_stall)
                {
                    // update stall statistics:
                    if (current_cycle() > inst->memory_stall_start_cycle)
                    {
                        inst->total_isolated_memory_stall_cycles += current_cycle() - inst->memory_stall_start_cycle;
                        inst->memory_stall_start_cycle = std::numeric_limits<uint64_t>::max();
                    }

                    process_execution_result(c, inst, result);
                    inst_waiting_for_memory_[i].first = nullptr;  // set to nullptr to indicate that this entry is invalid
                }
            }
        }
        
        auto inst_it = std::remove_if(inst_waiting_for_memory_.begin(), inst_waiting_for_memory_.end(),
                                    [](const auto& pair) { return pair.first == nullptr; });
        inst_waiting_for_memory_.erase(inst_it, inst_waiting_for_memory_.end());
    }
    else if (type == RETRY_TYPE::RESOURCE)
    {
        if (inst_waiting_for_resource_.empty())
            return;

        auto [inst, client_id] = inst_waiting_for_resource_.front();
        auto result = execute_instruction(clients_[client_id], inst);
        if (!result.is_resource_stall)
        {
            // update stall statistics:
            if (current_cycle() > inst->resource_stall_start_cycle)
            {
                inst->total_isolated_resource_stall_cycles += current_cycle() - inst->resource_stall_start_cycle;
                inst->resource_stall_start_cycle = std::numeric_limits<uint64_t>::max();
            }

            inst->resource_stall_start_cycle = std::numeric_limits<uint64_t>::max();

            process_execution_result(clients_[client_id], inst, result);
            inst_waiting_for_resource_.pop_front();
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<PATCH>::iterator
COMPUTE::find_patch_containing_qubit(QUBIT q)
{
    return std::find_if(patches_.begin(), patches_.end(),
                        [q] (const PATCH& p) { return p.contents == q; });
}

std::vector<PATCH>::const_iterator
COMPUTE::find_patch_containing_qubit_c(QUBIT q) const
{
    return std::find_if(patches_.begin(), patches_.end(),
                        [q] (const PATCH& p) { return p.contents == q; });
}

std::vector<MEMORY_MODULE*>::iterator
COMPUTE::find_memory_module_containing_qubit(QUBIT q)
{
    return std::find_if(mem_modules_.begin(), mem_modules_.end(),
                        [q] (MEMORY_MODULE* m) { return std::get<0>(m->find_qubit(q)); });
}

void
COMPUTE::access_memory_and_die_if_qubit_not_found(inst_ptr inst, QUBIT q, bool is_prefetch)
{
    auto module_it = find_memory_module_containing_qubit(q);
    if (module_it == mem_modules_.end())
    {
        std::cerr << "compute patches:\n";
        for (size_t i = compute_start_idx_; i < memory_start_idx_; i++)
            std::cerr << "\t" << patches_[i].contents << "\n";

        for (size_t i = 0; i < mem_modules_.size(); i++)
        {
            std::cerr << "memory module " << i << "------------------\n";
            mem_modules_[i]->dump_contents();
        }

        throw std::runtime_error("qubit " + q.to_string() + " not found in any memory module");
    }
    (*module_it)->initiate_memory_access(inst, q, is_prefetch);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class ITER> double
_weighted_window_depth(ITER begin_it, ITER it)
{
    return std::transform_reduce(begin_it, it, double{0.0}, std::plus<double>(),
                                [] (COMPUTE::inst_ptr x) -> double
                                {
                                    if (x->type == INSTRUCTION::TYPE::RZ || x->type == INSTRUCTION::TYPE::RX)
                                        return x->urotseq.size();
                                    else if (x->type == INSTRUCTION::TYPE::CCX || x->type == INSTRUCTION::TYPE::CCZ)
                                        return NUM_CCX_UOPS;
                                    else if (is_software_instruction(x->type))
                                        return 0.0;
                                    else
                                        return 2.0;
                                });
}

void
COMPUTE::try_rz_directed_prefetch(client_ptr& c, inst_ptr inst)
{
    /*
        Explanation: if this gate is an RZ or RX gate, then we will prefetch the qubit operand for the next RZ or RX gate
        in instruction order.
    */
    if (inst->has_initiated_prefetch || (inst->type != INSTRUCTION::TYPE::RZ && inst->type != INSTRUCTION::TYPE::RX))
        return;

    std::unordered_set<qubit_type> qubits_in_cmp;
    for (size_t i = compute_start_idx_; i < memory_start_idx_; i++)
    {
        if (patches_[i].contents.client_id == c->id)
            qubits_in_cmp.insert(patches_[i].contents.qubit_id);
    }

    QUBIT q{c->id, inst->qubits[0]};
    const auto& win = inst_windows_[q];

    if (win.empty())
        throw std::runtime_error("rz_directed_prefetch: no window found for " + q.to_string());

    std::unordered_set<qubit_type> pf_targets;
    size_t pf_limit{1};
    for (auto it = win.begin()+1; it != win.end(); it++)
    {
        // only care about multi qubit instructions:
        inst_ptr pf_inst = *it;
        if (pf_inst->qubits.size() == 1)
            continue;

        // only care about instructions that are not scheduled:
        if (pf_inst->is_scheduled)
            continue;
            
        // only care about instructions that have not initiated a prefetch:
        if (pf_inst->has_pending_prefetch_request)
            continue;

        for (qubit_type qid : pf_inst->qubits)
        {
            if (qubits_in_cmp.find(qid) == qubits_in_cmp.end())
                pf_targets.insert(qid);
        }

        pf_inst->has_initiated_prefetch = true;

        pf_limit--;
        if (!pf_limit)
            break;
    }

    if (pf_targets.empty())
        return;

    inst->has_initiated_prefetch = true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
update_free_times_along_routing_path(std::vector<ROUTING_COMPONENT::ptr_type>& path, 
                                            uint64_t cmp_cycle_free_bulk, 
                                            uint64_t cmp_cycle_free_endpoints)
{
    for (size_t i = 0; i < path.size(); i++)
    {
        if (i == 0 || i == path.size()-1)
            path[i]->cycle_free = cmp_cycle_free_endpoints;
        else
            path[i]->cycle_free = cmp_cycle_free_bulk;
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

bool
is_t_like_instruction(INSTRUCTION::TYPE type)
{
    return type == INSTRUCTION::TYPE::T
        || type == INSTRUCTION::TYPE::TDG
        || type == INSTRUCTION::TYPE::TX
        || type == INSTRUCTION::TYPE::TXDG;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim