/*
    author: Suhas Vittal
    date:   2025 August 18
*/

#include "sim.h"

#include <random>

std::mt19937 GL_RNG{0};

constexpr size_t NUM_CCZ_UOPS = 13;
constexpr size_t NUM_CCX_UOPS = NUM_CCZ_UOPS+2;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

SIM::clk_info::clk_info(double freq_compute_khz, double freq_target_khz)
    :clk_scale(freq_compute_khz/freq_target_khz - 1.0),
    leap(0.0)
{}

bool
SIM::clk_info::update_post_cpu_tick()
{
    bool yes = (leap < 1e-18);
    leap += yes ? clk_scale : -1.0;
    return yes;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
_acc_fact_count(const std::vector<size_t>& num_factories_by_level)
{
    return std::reduce(num_factories_by_level.begin(), num_factories_by_level.end(), size_t{0});
}

SIM::SIM(CONFIG cfg)
    :clients_(cfg.client_trace_files.size()),
    compute_((cfg.num_rows+1) * cfg.patches_per_row),
    inst_warmup_(cfg.inst_warmup),
    inst_sim_(cfg.inst_sim),
    compute_speed_khz_(1.0 / (cfg.compute_syndrome_extraction_time_ns * cfg.compute_rounds_per_cycle)),
    required_msfact_level_(cfg.num_15to1_factories_by_level.size() - 1)
{
    // initialize the magic state factories:
    init_t_state_factories(cfg);

    // initialize routing space:
    init_routing_space(cfg);

    // initialize the compute memory:
    init_compute(cfg);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

constexpr size_t NUM_STALL_TYPES{3};
// these are stall array indices:
constexpr size_t MEMORY_STALL_IDX{0};
constexpr size_t ROUTING_STALL_IDX{1};
constexpr size_t RESOURCE_STALL_IDX{2};

std::array<size_t, NUM_STALL_TYPES>
_count_stall_types(const std::vector<EXEC_RESULT>& exec_results)
{
    std::array<size_t, NUM_STALL_TYPES> stall_counts{};
    stall_counts[MEMORY_STALL_IDX] = std::count_if(exec_results.begin(), exec_results.end(),
                                                   [] (EXEC_RESULT r) { return r == EXEC_RESULT::MEMORY_STALL; });
    stall_counts[ROUTING_STALL_IDX] = std::count_if(exec_results.begin(), exec_results.end(),
                                                   [] (EXEC_RESULT r) { return r == EXEC_RESULT::ROUTING_STALL; });
    stall_counts[RESOURCE_STALL_IDX] = std::count_if(exec_results.begin(), exec_results.end(),
                                                   [] (EXEC_RESULT r) { return r == EXEC_RESULT::RESOURCE_STALL; });
    return stall_counts;
}

void
SIM::tick()
{
    if (warmup_)
    {
        // check if all clients have completed `inst_warmup_` instructions:
        bool all_clients_done_with_warmup = std::all_of(clients_.begin(), clients_.end(),
                                                    [] (const sim::CLIENT& c) 
                                                    { 
                                                        return c.s_inst_done >= inst_warmup_;
                                                    });
        if (all_clients_done_with_warmup)
        {
            warmup_ = false;
            // reset their stats:
            for (sim::CLIENT& c : clients_)
            {
                c.s_inst_done = 0;
                c.s_cycles_stalled = 0;
                c.s_cycles_stalled_by_mem = 0;
                c.s_cycles_stalled_by_routing = 0;
                c.s_cycles_stalled_by_resource = 0;
            }
        }
    }

    // tick each client:
    for (sim::CLIENT& c : clients_)
    {
        exec_results_.clear();

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
            c.s_cycles_stalled++;
            c.s_cycles_stalled_by_mem += (stall_counts[MEMORY_STALL_IDX] > 0);
            c.s_cycles_stalled_by_routing += (stall_counts[ROUTING_STALL_IDX] > 0);
            c.s_cycles_stalled_by_resource += (stall_counts[RESOURCE_STALL_IDX] > 0);
        }
    }

    GL_CYCLE++;

    // tick magic state factories:
    // since the frequency might be different, we need to account for this:
    for (size_t i = 0; i < t_fact_.size(); i++)
    {
        auto& fact_clk = t_fact_clk_info_[i];
        sim::T_FACTORY& fact = t_fact_[i];

        if (fact_clk.update_post_cpu_tick())
            fact.tick();
    }

    // tick the memory (TODO: make the memory system):

    done_ = !warmup_ && std::all_of(clients_.begin(), clients_.end(),
                                    [] (const sim::CLIENT& c) { return c.s_inst_done >= inst_sim_; });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
SIM::init_t_state_factories(const CONFIG& cfg)
{
    size_t num_15to1_factories = _acc_fact_count(cfg.num_15to1_factories_by_level);
    size_t num_factories = num_15to1_factories;
    t_fact_.reserve(num_factories);

    // create this on the first pass -- will need this for connecting 
    // upper level factories to lower level factories:
    std::unordered_map<size_t, std::vector<sim::T_FACTORY*>> level_to_fact_ptrs;
    level_to_fact_ptrs.reserve(cfg.num_15to1_factories_by_level.size());

    size_t patch_idx{0};
    for (size_t i = 0; i < cfg.num_15to1_factories_by_level.size(); i++)
    {
        for (size_t j = 0; j < cfg.num_15to1_factories_by_level[i]; j++)
        {
            sim::T_FACTORY f = sim::T_FACTORY::f15to1(i, cfg.t_round_ns, 4, patch_idx);
            t_fact_.push_back(f);
            t_fact_clk_info_.push_back(sim::clk_info(compute_speed_khz_, f.freq_khz));

            level_to_fact_ptrs[f.level].push_back(&t_fact_.back());
        }
    }

    // connect the magic state factories to each other:
    for (auto& f : t_fact_)
    {
        if (f->level > 0)
            f->resource_producers = level_to_fact_ptrs[f->level-1];
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
SIM::init_routing_space(const CONFIG& cfg)
{
    // number of junctions is num_rows + 1
    // number of buses is 2*num_rows + 1
    std::vector<sim::ROUTING_BASE::ptr_type> junctions(cfg.num_rows + 1);
    std::vector<sim::ROUTING_BASE::ptr_type> buses(2*cfg.num_rows + 1);
    for (size_t i = 0; i < junctions.size(); i++)
        junctions[i] = sim::ROUTING_BASE::ptr_type(new sim::ROUTING_BASE{sim::ROUTING_BASE::TYPE::JUNCTION});
    for (size_t i = 0; i < buses.size(); i++)
        buses[i] = sim::ROUTING_BASE::ptr_type(new sim::ROUTING_BASE{sim::ROUTING_BASE::TYPE::BUS});

    // connect the junctions and buses:
    auto connect_jb = [] (sim::ROUTING_BASE::ptr_type j, sim::ROUTING_BASE::ptr_type b)
    {
        j->connections.push_back(b);
        b->connections.push_back(j);
    };

    for (size_t i = 0; i < cfg.num_rows; i++)
    {
        connect_jb(junctions[i], buses[2*i]);
        connect_jb(junctions[i], buses[2*i+1]);
        connect_jb(junctions[i+1], buses[2*i+1]);  // `i+1` and `2*i+2` will connect next iteration
    }
    // and the last remaining pair:
    connect_jb(junctions[cfg.num_rows], buses[2*cfg.num_rows]);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
SIM::init_compute(const CONFIG& cfg)
{
    size_t patch_idx{0};

    // First setup the magic state pins:
    std::vector<sim::T_FACTORY*> top_level_t_fact(cfg.num_15to1_factories_by_level[required_msfact_level_]);
    for (size_t i = 0; i < t_fact_.size(); i++)
    {
        if (t_fact_[i].level == required_msfact_level_)
            top_level_t_fact[i] = &t_fact_[i];
    }

    if (top_level_t_fact.size() > cfg.patches_per_row+1)
        throw std::runtime_error("Not enough space to allocate all magic state pins");

    for (size_t i = 0; i < top_level_t_fact.size(); i++)
    {
        // `i == 0` will connect to a junction immediately
        top_level_t_fact[i]->output_patch_idx = patch_idx;
        if (i == 0)
            compute_[patch_idx].buses.push_back(junctions[0]);
        else
            compute_[patch_idx].buses.push_back(buses[0]);
        patch_idx++;
    }

    // now connect the program memory patches:
    for (size_t i = 0; i < cfg.num_rows; i++)
    {
        for (size_t j = 0; j < cfg.patches_per_row; j++)
        {
            // buses[i] is the upper bus, buses[i+1] is the left bus, buses[i+2] is the right bus
            bool is_upper = (j < cfg.patches_per_row/2);
            bool is_left = (j == 0 || j == cfg.patches_per_row/2);

            if (is_upper)
                compute_[patch_idx].buses.push_back(buses[2*i]);
            else
                compute_[patch_idx].buses.push_back(buses[2*i+2]);

            if (is_left)
                compute_[patch_idx].buses.push_back(buses[2*i+1]);
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// returns true if the instruction is deleted:
bool
_update_instruction_and_delete_on_completion(inst_ptr inst)
{
    if (inst->cycle_done <= GL_CYCLE)
    {
        delete inst;
        return true;
    }
    else
    {
        inst->cycle_done--;
        return false;
    }
}

void
_retire_instruction(sim::CLIENT& c, inst_ptr inst)
{
    // update stats:
    c.s_inst_done++;
    delete inst;
}

void
SIM::client_try_retire(sim::CLIENT& c)
{
    // retire any instructions at the head of a window,
    // or update the number of cycles until done
    for (auto& q : c.qubits)
    {
        if (q.inst_window.empty())
            continue;

        inst_ptr& inst = q.inst_window.front();
        // if the instruction has uops, we need to retire them.
        // once there are no uops left, we can retire the instruction.
        if (inst->num_uops > 0)
        {
            if (_update_instruction_and_delete_on_completion(inst->curr_uop))
            {
                inst->curr_uop = nullptr;
                inst->uop_completed++;
                inst->is_running = false;
                if (inst->uop_completed == inst->num_uops)
                {
                    _retire_instruction(c, inst);
                    q.inst_window.pop_front();
                }
            }
        }
        else if (_update_instruction_and_delete_on_completion(inst))
        {
            _retire_instruction(c, inst);
            q.inst_window.pop_front();
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
SIM::client_try_execute(sim::CLIENT& c)
{
    // check if any instruction is ready to be executed. 
    //    An instruction is ready to be executed if it is at the head of all its arguments' windows.
    for (auto& q : c.qubits)
    {
        // We will keep selecting instructions until we find one that is not a ready software instruction
select_inst:
        if (q.inst_window.empty())
            continue;

        inst_ptr& inst = q.inst_window.front();

        bool all_ready = std::all_of(inst->qubits.begin(), inst->qubits.end(), 
                                    [&c] (qubit_type id) 
                                    {
                                        return c.qubits[id].inst_window.front() == inst;
                                    });

        // if the instruction is a software instruction, we can complete it immediately
        if (inst->type == INSTRUCTION::TYPE::X 
            || inst->type == INSTRUCTION::TYPE::Y
            || inst->type == INSTRUCTION::TYPE::Z
            || inst->type == INSTRUCTION::TYPE::SWAP)
        {
            _retire_instruction(c, inst);
            goto select_inst;
        }

        // TODO: we may need to decompose the instruction

        if (all_ready && !inst->is_running)
        {
            auto result = execute_instruction(c, inst);
            exec_results_.push_back(result);
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
SIM::client_try_fetch(sim::CLIENT& c)
{
    // read instructions from the trace and add them to instruction windows.
    // stop when all windows have at least one instruction.
    auto it = std::find_if(c.qubits.begin(), c.qubits.end(),
                           [] (const auto& q) { return q.inst_window.empty(); });
    while (it != c.qubits.end())
    {
        qubit_type target_qubit = it->qubit_id;
        while (true) // keep going until we get an instruction that operates on `target_qubit`
        {
            inst_ptr inst{new INSTRUCTION(c.read_instruction_from_trace())};
            c.s_inst_read++;

            // set the number of uops (may depend on simulator config)
            if (inst->type == INSTRUCTION::TYPE::RX || inst->type == INSTRUCTION::TYPE::RZ)
                inst->num_uops = inst->urotseq.size();
            else if (inst->type == INSTRUCTION::TYPE::CCX || inst->type == INSTRUCTION::TYPE::CCZ)
                inst->num_uops = inst->type == INSTRUCTION::TYPE::CCX ? NUM_CCX_UOPS : NUM_CCZ_UOPS;
            else
                inst->num_uops = 0;

            // add the instruction to the windows of all the qubits it operates on
            for (qubit_type q : inst->qubits)
                c.qubits[q].inst_window.push_back(inst);

            // check if the instruction operates on `target_qubit`
            auto qubits_it = std::find(inst->qubits.begin(), inst->qubits.end(), target_qubit);
            if (qubits_it != inst->qubits.end())
                break;
        }

        // check again for any empty windows
        it = std::find_if(c.qubits.begin(), c.qubits.end(),
                           [] (const auto& q) { return q.inst_window.empty(); });
    }   
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

PATCH::bus_array::iterator
_find_free_bus(PATCH& p, uint64_t t_free)
{
    return std::find_if(p.buses.begin(), p.buses.end(), 
                        [t_free] (const auto& b) { return b->t_free <= t_free; });
}

// Returns true if the path was allocated, false otherwise.
bool
_allocate_bus_path_for_cx_like(PATCH& src, PATCH& dst, uint64_t endpoint_latency=2, uint64_t path_latency=2)
{
    // check if the bus is free
    auto src_it = _find_free_bus(src, GL_CYCLE);
    auto dst_it = _find_free_bus(dst, GL_CYCLE);
    if (src_it == src.buses.end() || dst_it == dst.buses.end())
        return false;

    // now check if we can route:
    auto path = route_path_from_src_to_dst(*src_it, *dst_it);
    for (auto& r : path)
        r->t_free = GL_CYCLE + path_latency;

    // hold the endpoints for `endpoint_latency` cycles:
    (*src_it)->t_free = GL_CYCLE + endpoint_latency;
    (*dst_it)->t_free = GL_CYCLE + endpoint_latency;

    return true;
}

EXEC_RESULT
SIM::execute_instruction(sim::CLIENT& c, inst_ptr inst)
{
    // check if all qubits are in compute memory:
    for (qubit_type qid : inst->qubits)
    {
        const auto& q = clients_[client_id].qubits[qid];

        // ensure all instructions are ready:
        if (q.memloc_info.t_free > GL_CYCLE)
            return EXEC_RESULT::WAITING_FOR_QUBIT_TO_BE_READY;

        // if a qubit is not in memory, we need to make a memory request:
        if (q.memloc_info.where == MEMINFO::LOCATION::MEMORY)
        {
            // set `t_until_in_compute` and `t_free` 
            // to `std::numeric_limits<uint64_t>::max()` to indicate that
            // this is blocked:
            q.memloc_info.t_until_in_compute = std::numeric_limits<uint64_t>::max();
            q.memloc_info.t_free = std::numeric_limits<uint64_t>::max();
            // TODO: make memory request and exit
            return EXEC_RESULT::MEMORY_STALL;
        }
    }

    EXEC_RESULT result{EXEC_RESULT::SUCCESS};

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

        // check if the bus is free
        QUBIT& q = c.qubits[inst->qubits[0]];
        PATCH& p = compute_[q.memloc_info.patch_idx];
        auto it = _find_free_bus(p, GL_CYCLE);
        if (it == p.buses.end())
        {
            result = EXEC_RESULT::ROUTING_STALL;
        }
        else
        {
            q.memloc_info.t_free = GL_CYCLE + 2;
            (*it)->t_free = GL_CYCLE + 2;
            inst->cycle_done = GL_CYCLE + 2;
            inst->is_running = true;
        }
    }
    else if (inst->type == INSTRUCTION::TYPE::CX)
    {
        // this is a 2-cycle gate that requires the bus:
        // As we allocate an ancilla on the bus that needs to connect to the control and target
        // qubits, we need to route from the control to the target and occupy all bus components.
        // on the path.
        QUBIT& ctrl = c.qubits[inst->qubits[0]];
        QUBIT& target = c.qubits[inst->qubits[1]];

        PATCH& c_patch = compute_[ctrl.memloc_info.patch_idx];
        PATCH& t_patch = compute_[target.memloc_info.patch_idx];

        if (_allocate_bus_path_for_cx_like(c_patch, t_patch))
        {
            inst->cycle_done = GL_CYCLE + 2;
            inst->is_running = true;
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

        // keep trying until we succeed or there is no factory that has a resource state:
        bool any_factory_has_resource{false};
        for (size_t i = 0; i < t_fact_.size() && !inst->is_running; i++)
        {
            auto& fact = t_fact_[i];
            if (fact.level != required_msfact_level_ || fact.buffer_occu == 0)
                continue;

            any_factory_has_resource = true;
            // get the factory's output patch and consume the magic state:
            PATCH& f_patch = compute_[fact.output_patch_idx];

            QUBIT& q = c.qubits[inst->qubits[0]];
            PATCH& p = compute_[q.memloc_info.patch_idx];

            if (_allocate_bus_path_for_cx_like(f_patch, p, endpoint_latency, path_latency))
            {
                inst->cycle_done = GL_CYCLE + 2 + 2*static_cast<int>(clifford_correction);
                inst->is_running = true;

                fact.buffer_occu--;
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
        size_t uop_idx = inst->uop_completed;
        inst->curr_uop = new INSTRUCTION(inst->urotseq[uop_idx], inst->qubits);
        result = execute_instruction(client_id, inst->curr_uop);
        inst->is_running = (result == EXEC_RESULT::SUCCESS);
    }
    else if (inst->type == INSTRUCTION::TYPE::CCX || inst->type == INSTRUCTION::TYPE::CCZ)
    {
        using uop_spec_type = std::pair<INSTRUCTION::TYPE, std::initializer_list<size_t>>;

        // gate declaration to make this very simple:
        constexpr INSTRUCTION::TYPE CX = INSTRUCTION::TYPE::CX;
        constexpr INSTRUCTION::TYPE TDG = INSTRUCTION::TYPE::TDG;
        constexpr INSTRUCTION::TYPE T = INSTRUCTION::TYPE::T;
        constexpr uop_spec_type CCZ_UOPS[]
        {
            {CX, {1,2}},
            {TDG, {2}},
            {CX, {0,2}},
            {T, {2}},
            {CX, {1,2}},
            {T, {1}},
            {TDG, {2}},
            {CX, {0,2}},
            {T, {2}},
            {CX, {0,1}},
            {T, {0}},
            {TDG, {1}},
            {CX, {0,1}}
        };

        // depending on simulator config, we will want to use magic states or synthillation

        // T state implementation:
        uint64_t uop_idx = inst->uop_completed;
        if (inst->type == INSTRUCTION::TYPE::CCX)
        {
            if (uop_idx == 0 || uop_idx == NUM_CCX_UOPS-1)
            {
                inst->curr_uop = new INSTRUCTION(INSTRUCTION::TYPE::H, {inst->qubits[2]});
                goto ccxz_execute_uop;
            }

            // otherwise, decrement `uop_idx` by 2 so we can index into the `CCZ_UOPS` array:
            uop_idx -= 2;
        }
        inst->curr_uop = new INSTRUCTION(CCZ_UOPS[uop_idx].first, CCZ_UOPS[uop_idx].second);

ccxz_execute_uop:
        result = execute_instruction(client_id, inst->curr_uop);
        inst->is_running = (result == EXEC_RESULT::SUCCESS);
    }
    else if (inst->type == INSTRUCTION::TYPE::MZ || inst->type == INSTRUCTION::TYPE::MX)
    {
        // takes one cycle to complete, and doesn't require any routing/resources
        inst->cycle_done = GL_CYCLE + 1;
        inst->is_running = true;
    }
        
    return EXEC_RESULT::SUCCESS;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////