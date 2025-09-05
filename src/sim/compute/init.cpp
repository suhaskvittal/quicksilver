/*
    author: Suhas Vittal
    date:   27 August 2025

    This file only contains the initialization private functions for `COMPUTE`.
*/

#include "sim/compute.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE::init_clients(const CONFIG& cfg)
{
    clients_.reserve(cfg.client_trace_files.size());

    size_t patch_idx{patch_idx_compute_start_};
    size_t mem_idx{0};  // use this once all compute patches are filled

    for (size_t i = 0; i < cfg.client_trace_files.size(); i++)
    {
        client_ptr c{new sim::CLIENT(cfg.client_trace_files[i])};
        for (auto& q : c->qubits)
        {
            if (patch_idx >= patch_idx_memory_start_)
            {
                // start putting qubits in memory:
                MEMORY_MODULE* m = memory_[mem_idx];
                
                // search for uninitialized qubit in the module:
                auto m_it = m->find_uninitialized_qubit();
                
                // if no uninitialized qubit is found, we need to go to the next module:
                if (m_it == m->contents.end())
                {
                    // goto next module:
                    mem_idx++;

                    if (mem_idx >= memory_.size())
                        throw std::runtime_error("Not enough space in memory to allocate all qubits");

                    m = memory_[mem_idx];
                    m_it = m->find_uninitialized_qubit();
                }
                
                *m_it = std::make_pair(c.get(), q.memloc_info.qubit_id);

                // update qubit info:
                q.memloc_info.where = MEMINFO::LOCATION::MEMORY;
            }
            else
            {
                // set patch information:
                patches_[patch_idx++].client = c.get();
                patches_[patch_idx++].qubit_id = q.memloc_info.qubit_id;
                
                // update qubit info:
                q.memloc_info.where = MEMINFO::LOCATION::COMPUTE;
            }
        }
        clients_.push_back(std::move(c));
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE::bus_info
COMPUTE::init_routing_space(const CONFIG& cfg)
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
COMPUTE::init_compute(const CONFIG& cfg, const bus_array& junctions, const bus_array& buses)
{
    size_t patch_idx{patches_reserved_for_resource_pins_};

    // First setup the magic state pins:
    std::vector<sim::T_FACTORY*> top_level_t_fact;
    std::copy_if(t_fact_.begin(), t_fact_.end(), std::back_inserter(top_level_t_fact),
                [cfg] (T_FACTORY* f) { return f->level == cfg.target_t_fact_level; });

    std::cout << "top_level_t_fact.size() = " << top_level_t_fact.size() << "\n";

    if (top_level_t_fact.size() > cfg.patches_per_row+2)
        throw std::runtime_error("Not enough space to allocate all magic state pins");

    for (size_t i = 0; i < top_level_t_fact.size(); i++)
    {
        auto* fact = top_level_t_fact[i];
        
        // set the output patch index:
        fact->output_patch_idx = patch_idx;

        // create bus and junction connections:
        PATCH& fp = patches_[patch_idx];
        fp.buses.push_back(buses[0]);
        if (i == 0)
            fp.buses.push_back(junctions[0]);
        else if (i == cfg.patches_per_row+1)
            fp.buses.push_back(junctions[1]);
        else
            fp.buses.push_back(buses[0]);

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
                patches_[patch_idx].buses.push_back(buses[2*i]);
            else
                patches_[patch_idx].buses.push_back(buses[2*i+2]);

            if (is_left)
                patches_[patch_idx].buses.push_back(buses[2*i+1]);

            patch_idx++;
        }
    }

    // set the connections for the memory pins:
    const size_t last_bus_idx = 3*cfg.num_rows;
    const size_t penult_junc_idx = 2*cfg.num_rows;
    const size_t last_junc_idx = 2*cfg.num_rows+1;

    const auto& mem = memory_->modules();
    if (mem.size() > cfg.patches_per_row+2)
        throw std::runtime_error("Not enough space to allocate all memory pins");

    for (size_t i = 0; i < mem.size(); i++)
    {
        auto* m = mem[i];
        m->output_patch_idx = patch_idx;
        
        PATCH& mp = patches_[patch_idx];
        if (i == 0)
            mp.buses.push_back(junctions[penult_junc_idx]);
        else if (i == cfg.patches_per_row+1)
            mp.buses.push_back(junctions[last_junc_idx]);
        else
            mp.buses.push_back(buses[last_bus_idx]);

        patch_idx++;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim