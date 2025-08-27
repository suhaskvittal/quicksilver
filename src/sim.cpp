/*
    author: Suhas Vittal
    date:   2025 August 18
*/

#include "sim.h"

#include <random>

uint64_t     GL_CYCLE{0};
std::mt19937 GL_RNG{0};

constexpr size_t NUM_CCZ_UOPS = 13;
constexpr size_t NUM_CCX_UOPS = NUM_CCZ_UOPS+2;

static std::uniform_real_distribution<double> FP_RAND{0.0, 1.0};

//#define QS_SIM_DEBUG

constexpr uint64_t QS_SIM_DEBUG_CYCLE_INTERVAL = 100'000;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
_is_software_instruction(INSTRUCTION::TYPE type)
{
    return type == INSTRUCTION::TYPE::X 
        || type == INSTRUCTION::TYPE::Y
        || type == INSTRUCTION::TYPE::Z
        || type == INSTRUCTION::TYPE::SWAP;
}

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
    :compute_((cfg.num_rows+1) * cfg.patches_per_row),
    inst_warmup_(cfg.inst_warmup),
    inst_sim_(cfg.inst_sim),
    compute_speed_khz_(1e6 / (cfg.compute_syndrome_extraction_time_ns * cfg.compute_rounds_per_cycle)),
    target_t_fact_level_(cfg.num_15to1_factories_by_level.size() - 1)
{
    // initialize the magic state factories:
    init_t_state_factories(cfg);

    // initialize routing space:
    auto [junctions, buses] = init_routing_space(cfg);

    // initialize the compute memory:
    init_compute(cfg, junctions, buses);

    // finally, initialize the clients: we need to assign each client's qubits to patches
    size_t total_qubits_required = std::transform_reduce(clients_.begin(), clients_.end(), 
                                            size_t{0}, 
                                            std::plus<size_t>{},
                                            [] (const client_ptr& c) { return c->qubits.size(); });
    size_t avail_patches = compute_.size() - patches_reserved_for_resource_pins_;
    if (avail_patches < total_qubits_required)
        throw std::runtime_error("Not enough space to allocate all program qubits");

    size_t patch_idx{patches_reserved_for_resource_pins_};
    clients_.reserve(cfg.client_trace_files.size());
    for (size_t i = 0; i < cfg.client_trace_files.size(); i++)
    {
        client_ptr c{new sim::CLIENT(cfg.client_trace_files[i])};
        for (auto& q : c->qubits)
            q.memloc_info.patch_idx = patch_idx++;
        clients_.push_back(std::move(c));
    }
}

SIM::~SIM()
{
    for (auto* f : t_fact_)
        delete f;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

constexpr size_t NUM_STALL_TYPES{3};
// these are stall array indices:
constexpr size_t MEMORY_STALL_IDX{0};
constexpr size_t ROUTING_STALL_IDX{1};
constexpr size_t RESOURCE_STALL_IDX{2};

std::array<size_t, NUM_STALL_TYPES>
_count_stall_types(const std::vector<SIM::EXEC_RESULT>& exec_results)
{
    auto _count = [&exec_results] (SIM::EXEC_RESULT r) 
                    { 
                        return std::count_if(exec_results.begin(), exec_results.end(), [r] (SIM::EXEC_RESULT rr) { return rr == r; });
                    };
    std::array<size_t, NUM_STALL_TYPES> stall_counts{};
    stall_counts[MEMORY_STALL_IDX] = _count(SIM::EXEC_RESULT::MEMORY_STALL);
    stall_counts[ROUTING_STALL_IDX] = _count(SIM::EXEC_RESULT::ROUTING_STALL);
    stall_counts[RESOURCE_STALL_IDX] = _count(SIM::EXEC_RESULT::RESOURCE_STALL);
    return stall_counts;
}

void
SIM::tick()
{
    if (warmup_)
    {
        // check if all clients have completed `inst_warmup_` instructions:
        bool all_clients_done_with_warmup = std::all_of(clients_.begin(), clients_.end(),
                                                    [w=inst_warmup_] (const client_ptr& c) 
                                                    { 
                                                        return c->s_inst_done >= w;
                                                    });
        if (all_clients_done_with_warmup)
        {
            warmup_ = false;
            // reset their stats:
            for (const client_ptr& c : clients_)
            {
                c->s_inst_done = 0;
                c->s_unrolled_inst_done = 0;
                c->s_cycles_stalled = 0;
                c->s_cycles_stalled_by_mem = 0;
                c->s_cycles_stalled_by_routing = 0;
                c->s_cycles_stalled_by_resource = 0;
            }
            std::cout << "warmup done\n";
        }
    }
#if defined(QS_SIM_DEBUG)
    if (GL_CYCLE % QS_SIM_DEBUG_CYCLE_INTERVAL == 0)
    {
        std::cout << "-----------------------------------\n";
        std::cout << "GL_CYCLE = " << GL_CYCLE << "\n";
    }
#endif

    // tick each client:
    for (size_t i = 0; i < clients_.size(); i++)
    {
        client_ptr& c = clients_[i];
        exec_results_.clear();

#if defined(QS_SIM_DEBUG)
        if (GL_CYCLE % QS_SIM_DEBUG_CYCLE_INTERVAL == 0)
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

    GL_CYCLE++;

    // tick magic state factories:
    // since the frequency might be different, we need to account for this:
    for (size_t i = 0; i < t_fact_.size(); i++)
    {
        auto& fact_clk = t_fact_clk_info_[i];
        sim::T_FACTORY* fact = t_fact_[i];

        if (fact_clk.update_post_cpu_tick())
        {
            fact->tick();

#if defined(QS_SIM_DEBUG)
            if (GL_CYCLE % QS_SIM_DEBUG_CYCLE_INTERVAL == 0)
            {
            // print factory state:
                std::cout << "FACTORY " << i
                << " (level = " << fact->level 
                    << "): occu = " << fact->buffer_occu 
                    << ", step = " << fact->step << "\n";
            }
#endif
        }
    }

    // tick the memory (TODO: make the memory system):

    done_ = !warmup_ && std::all_of(clients_.begin(), clients_.end(),
                                    [s=inst_sim_] (const client_ptr& c) { return c->s_inst_done >= s; });
    if (done_)
    {
        std::cout << "sim done\n";
    }
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
            sim::T_FACTORY* f = new sim::T_FACTORY
                                {
                                    sim::T_FACTORY::f15to1(i, cfg.compute_syndrome_extraction_time_ns, 4, patch_idx)
                                };

            t_fact_.push_back(f);
            t_fact_clk_info_.push_back(clk_info(compute_speed_khz_, f->freq_khz));

            level_to_fact_ptrs[f->level].push_back(f);

            if (i == target_t_fact_level_)
            {
                patches_reserved_for_resource_pins_++;
                patch_idx++;
            }
        }
    }

    // connect the magic state factories to each other:
    for (auto* f : t_fact_)
    {
        if (f->level > 0)
            f->resource_producers = level_to_fact_ptrs[f->level-1];
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

SIM::bus_info
SIM::init_routing_space(const CONFIG& cfg)
{
    // number of junctions is 2*(num_rows + 1)
    // number of buses is 3*num_rows + 1
    std::vector<sim::ROUTING_BASE::ptr_type> junctions(2 * (cfg.num_rows + 1));
    std::vector<sim::ROUTING_BASE::ptr_type> buses(3*cfg.num_rows + 1);
    
    for (size_t i = 0; i < junctions.size(); i++)
        junctions[i] = sim::ROUTING_BASE::ptr_type(new sim::ROUTING_BASE{i, sim::ROUTING_BASE::TYPE::JUNCTION});

    for (size_t i = 0; i < buses.size(); i++)
        buses[i] = sim::ROUTING_BASE::ptr_type(new sim::ROUTING_BASE{i,sim::ROUTING_BASE::TYPE::BUS});

    // connect the junctions and buses:
    auto connect_jb = [] (sim::ROUTING_BASE::ptr_type j, sim::ROUTING_BASE::ptr_type b)
    {
        j->connections.push_back(b);
        b->connections.push_back(j);
    };

    for (size_t i = 0; i < cfg.num_rows; i++)
    {
        /*
            2i ----- 3i ------ 2i+1
            |                   |
           3i+1 ---------------- 3i+2
            |                   |
           2i+2 ---3i+3--------- 2i+3
        */        


        connect_jb(junctions[2*i], buses[3*i]);
        connect_jb(junctions[2*i], buses[3*i+1]);

        connect_jb(junctions[2*i+1], buses[3*i]);
        connect_jb(junctions[2*i+1], buses[3*i+2]);

        connect_jb(junctions[2*i+2], buses[3*i+1]);
        connect_jb(junctions[2*i+3], buses[3*i+2]);
    }
    // and the last remaining pair:
    connect_jb(junctions[2*cfg.num_rows], buses[3*cfg.num_rows]);
    connect_jb(junctions[2*cfg.num_rows+1], buses[3*cfg.num_rows]);

    return bus_info{junctions, buses};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
SIM::init_compute(const CONFIG& cfg, const bus_array& junctions, const bus_array& buses)
{
    size_t patch_idx{patches_reserved_for_resource_pins_};

    // First setup the magic state pins:
    std::vector<sim::T_FACTORY*> top_level_t_fact;
    top_level_t_fact.reserve(cfg.num_15to1_factories_by_level[target_t_fact_level_]);
    for (size_t i = 0; i < t_fact_.size(); i++)
    {
        if (t_fact_[i]->level == target_t_fact_level_)
            top_level_t_fact.push_back(t_fact_[i]);
    }

    if (top_level_t_fact.size() > cfg.patches_per_row+2)
        throw std::runtime_error("Not enough space to allocate all magic state pins");

    for (size_t i = 0; i < top_level_t_fact.size(); i++)
    {
        auto* fact = top_level_t_fact[i];
        // `i == 0` will connect to a junction immediately
        if (i == 0 || i == top_level_t_fact.size()-1)
            compute_[fact->output_patch_idx].buses.push_back(junctions[0]);
        else
            compute_[fact->output_patch_idx].buses.push_back(buses[0]);
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

            patch_idx++;
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// returns true if the instruction is deleted:
bool
_update_instruction_and_check_if_done(SIM::inst_ptr inst)
{
    if (inst->cycle_done <= GL_CYCLE)
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
_retire_instruction(SIM::client_ptr& c, SIM::inst_ptr inst)
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
SIM::client_try_retire(client_ptr& c)
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
            if (inst->curr_uop != nullptr && _update_instruction_and_check_if_done(inst->curr_uop))
            {
                delete inst->curr_uop;
                inst->curr_uop = nullptr;
                inst->uop_completed++;
                inst->is_running = false;
                if (inst->uop_completed == inst->num_uops)
                    _retire_instruction(c, inst);
            }
        }
        else if (_update_instruction_and_check_if_done(inst))
        {
            _retire_instruction(c, inst);
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
SIM::client_try_execute(client_ptr& c)
{
    // check if any instruction is ready to be executed. 
    //    An instruction is ready to be executed if it is at the head of all its arguments' windows.
    for (auto& q : c->qubits)
    {
        if (q.inst_window.empty())
            continue;

        inst_ptr& inst = q.inst_window.front();

#if defined(QS_SIM_DEBUG)
        if (GL_CYCLE % QS_SIM_DEBUG_CYCLE_INTERVAL == 0)
        {
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
        }
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
            if (GL_CYCLE % QS_SIM_DEBUG_CYCLE_INTERVAL == 0)
            {
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
            }
#endif
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
SIM::client_try_fetch(client_ptr& c)
{
#if defined(QS_SIM_DEBUG)
    if (GL_CYCLE % QS_SIM_DEBUG_CYCLE_INTERVAL == 0)
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
    if (GL_CYCLE % QS_SIM_DEBUG_CYCLE_INTERVAL == 0)
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
            if (GL_CYCLE % QS_SIM_DEBUG_CYCLE_INTERVAL == 0)
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

#if defined(QS_SIM_DEBUG)
    if (GL_CYCLE % QS_SIM_DEBUG_CYCLE_INTERVAL == 0)
        std::cout << "\t\tno more instructions to read\n";
#endif
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

sim::PATCH::bus_array::iterator
_find_free_bus(sim::PATCH& p, uint64_t t_free)
{
    return std::find_if(p.buses.begin(), p.buses.end(), 
                        [t_free] (const auto& b) { return b->t_free <= t_free; });
}

// Returns true if the path was allocated, false otherwise.
bool
_allocate_bus_path_for_cx_like(sim::PATCH& src, sim::PATCH& dst, uint64_t endpoint_latency=2, uint64_t path_latency=2)
{
    // check if the bus is free
    auto src_it = _find_free_bus(src, GL_CYCLE);
    auto dst_it = _find_free_bus(dst, GL_CYCLE);
    if (src_it == src.buses.end() || dst_it == dst.buses.end())
        return false;

    // now check if we can route:
    if (*src_it == *dst_it)
    {
        (*src_it)->t_free = GL_CYCLE + endpoint_latency;
    }
    else
    {
        auto path = sim::route_path_from_src_to_dst(*src_it, *dst_it);
        if (path.empty())
            return false;

        for (auto& r : path)
            r->t_free = GL_CYCLE + path_latency;

        // hold the endpoints for `endpoint_latency` cycles:
        (*src_it)->t_free = GL_CYCLE + endpoint_latency;
        (*dst_it)->t_free = GL_CYCLE + endpoint_latency;
    }

    return true;
}

SIM::EXEC_RESULT
SIM::execute_instruction(client_ptr& c, inst_ptr inst)
{
    // complete immediately -- do not have to wait for qubits to be ready
    if (_is_software_instruction(inst->type))
    {
        inst->cycle_done = GL_CYCLE + 1;
        inst->is_running = true;
        return EXEC_RESULT::SUCCESS;
    }

    // check if all qubits are in compute memory:
    for (qubit_type qid : inst->qubits)
    {
        auto& q = c->qubits[qid];

        // ensure all instructions are ready:
        if (q.memloc_info.t_free > GL_CYCLE)
            return EXEC_RESULT::WAITING_FOR_QUBIT_TO_BE_READY;

        // if a qubit is not in memory, we need to make a memory request:
        if (q.memloc_info.where == sim::MEMINFO::LOCATION::MEMORY)
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

#if defined(QS_SIM_DEBUG)
    if (GL_CYCLE % QS_SIM_DEBUG_CYCLE_INTERVAL == 0)
        std::cout << "\t\tall qubits are available -- trying to execute instruction: " << (*inst) << "\n";
#endif

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
        auto& q = c->qubits[inst->qubits[0]];
        auto& p = compute_[q.memloc_info.patch_idx];
        
#if defined(QS_SIM_DEBUG)
        if (GL_CYCLE % QS_SIM_DEBUG_CYCLE_INTERVAL == 0)
        {
            std::cout << "\t\tbuses near qubit " << inst->qubits[0] << " (patch = " << q.memloc_info.patch_idx << "):";
            for (auto& b : p.buses)
                std::cout << " " << b->t_free;
            std::cout << "\n";
        }
#endif

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
        auto& ctrl = c->qubits[inst->qubits[0]];
        auto& target = c->qubits[inst->qubits[1]];

        auto& c_patch = compute_[ctrl.memloc_info.patch_idx];
        auto& t_patch = compute_[target.memloc_info.patch_idx];

        if (_allocate_bus_path_for_cx_like(c_patch, t_patch))
        {
            inst->cycle_done = GL_CYCLE + 2;
            inst->is_running = true;

            ctrl.memloc_info.t_free = GL_CYCLE + 2;
            target.memloc_info.t_free = GL_CYCLE + 2;
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
        auto& p = compute_[q.memloc_info.patch_idx];

        // keep trying until we succeed or there is no factory that has a resource state:
        bool any_factory_has_resource{false};
        for (size_t i = 0; i < t_fact_.size() && !inst->is_running; i++)
        {
            auto* fact = t_fact_[i];
            if (fact->level != target_t_fact_level_ || fact->buffer_occu == 0)
                continue;

            any_factory_has_resource = true;
            // get the factory's output patch and consume the magic state:
            auto& f_patch = compute_[fact->output_patch_idx];

            if (_allocate_bus_path_for_cx_like(f_patch, p, endpoint_latency, path_latency))
            {
                inst->cycle_done = GL_CYCLE + 2 + 2*static_cast<int>(clifford_correction);
                inst->is_running = true;

                q.memloc_info.t_free = GL_CYCLE + 2 + 2*static_cast<int>(clifford_correction);

                fact->buffer_occu--;
            }
            else
            {
                result = EXEC_RESULT::ROUTING_STALL;
            }
        }

#if defined(QS_SIM_DEBUG)
            if (result == EXEC_RESULT::ROUTING_STALL)
            {
                if (GL_CYCLE % QS_SIM_DEBUG_CYCLE_INTERVAL == 0)
                {
                    std::cout << "\t\trouting stall, free buses near qubit:";
                    for (auto& b : p.buses)
                        std::cout << " " << b->t_free;
                    std::cout << "\n";
                }
            }
#endif

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
        inst->cycle_done = GL_CYCLE + 1;
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