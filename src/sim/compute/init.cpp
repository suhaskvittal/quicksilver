/*
    author: Suhas Vittal
    date:   27 August 2025

    This file only contains the initialization private functions for `COMPUTE`.
*/

#include "sim/compute.h"
#include "sim/memory.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE::init_clients(const std::vector<std::string>& client_trace_files)
{
    clients_.reserve(client_trace_files.size());

    size_t patch_idx{patch_idx_compute_start_};
    size_t mem_idx{0};  // use this once all compute patches are filled

    for (size_t i = 0; i < client_trace_files.size(); i++)
    {
        client_ptr c{new sim::CLIENT(client_trace_files[i], i)};
        for (auto& q : c->qubits)
        {
            if (patch_idx >= patch_idx_memory_start_)
            {
                // start putting qubits in memory:
                MEMORY_MODULE* m = memory_[mem_idx];
                
                // search for uninitialized qubit in the module:
                auto [b_it, q_it] = m->find_uninitialized_qubit();
                
                // if no uninitialized qubit is found, we need to go to the next module:
                if (b_it == m->banks_.end())
                {
                    // goto next module:
                    mem_idx++;

                    if (mem_idx >= memory_.size())
                        throw std::runtime_error("Not enough space in memory to allocate all qubits");

                    m = memory_[mem_idx];
                    std::tie(b_it, q_it) = m->find_uninitialized_qubit();
                }
                
                *q_it = std::make_pair(q.memloc_info.client_id, q.memloc_info.qubit_id);

                // update qubit info:
                q.memloc_info.where = MEMINFO::LOCATION::MEMORY;
            }
            else
            {
                // set patch information:
                patches_[patch_idx].client_id = q.memloc_info.client_id;
                patches_[patch_idx].qubit_id = q.memloc_info.qubit_id;
                
                // update qubit info:
                q.memloc_info.where = MEMINFO::LOCATION::COMPUTE;

                patch_idx++;
            }
        }
        clients_.push_back(std::move(c));
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE::bus_info
COMPUTE::init_routing_space(size_t num_rows, size_t num_patches_per_row)
{
    // number of junctions is 2*(num_rows + 1)
    // number of buses is 3*num_rows + 1
    std::vector<sim::ROUTING_BASE::ptr_type> junctions(2 * (num_rows + 1));
    std::vector<sim::ROUTING_BASE::ptr_type> buses(3*num_rows + 1);
    
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

    for (size_t i = 0; i < num_rows; i++)
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
    connect_jb(junctions[2*num_rows], buses[3*num_rows]);
    connect_jb(junctions[2*num_rows+1], buses[3*num_rows]);

    return bus_info{junctions, buses};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE::init_compute(size_t num_rows, size_t num_patches_per_row, const bus_array& junctions, const bus_array& buses)
{
    size_t patch_idx{0};

    // First setup the magic state pins:
    std::vector<sim::T_FACTORY*> top_level_t_fact;
    std::copy_if(t_fact_.begin(), t_fact_.end(), std::back_inserter(top_level_t_fact),
                [lvl=target_t_fact_level_] (T_FACTORY* f) { return f->level == lvl; });

    const size_t full_row_width = (num_patches_per_row/2) + 2;  // note that a row is 2 wide (upper and lower part)
    if (top_level_t_fact.size() > full_row_width)
    {
        throw std::runtime_error("Not enough space to allocate all magic state pins: " 
                                    + std::to_string(top_level_t_fact.size()) 
                                    + " > " + std::to_string(full_row_width));
    }

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
        else if (i == full_row_width-1)
            fp.buses.push_back(junctions[1]);
        else
            fp.buses.push_back(buses[0]);

        patch_idx++;
    }

    // now connect the program memory patches:
    for (size_t i = 0; i < num_rows; i++)
    {
        for (size_t j = 0; j < num_patches_per_row; j++)
        {
            // buses[i] is the upper bus, buses[i+1] is the left bus, buses[i+2] is the right bus
            bool is_upper = (j < num_patches_per_row/2);
            bool is_left = (j == 0 || j == num_patches_per_row/2);
            bool is_right = (j == num_patches_per_row/2-1 || j == num_patches_per_row-1);

            if (is_upper)
                patches_[patch_idx].buses.push_back(buses[3*i]);
            else
                patches_[patch_idx].buses.push_back(buses[3*i+3]);

            if (is_left)
                patches_[patch_idx].buses.push_back(buses[3*i+1]);
            if (is_right)
                patches_[patch_idx].buses.push_back(buses[3*i+2]);

            patch_idx++;
        }
    }

    // set the connections for the memory pins:
    const size_t last_bus_idx = 3*num_rows;
    const size_t penult_junc_idx = 2*num_rows;
    const size_t last_junc_idx = 2*num_rows+1;

    if (memory_.size() > full_row_width)
    {
        throw std::runtime_error("Not enough space to allocate all memory pins: " 
                                    + std::to_string(memory_.size()) 
                                    + " > " + std::to_string(full_row_width));
    }

    for (size_t i = 0; i < memory_.size(); i++)
    {
        auto* m = memory_[i];
        m->output_patch_idx = patch_idx;
        
        PATCH& mp = patches_[patch_idx];
        if (i == 0)
            mp.buses.push_back(junctions[penult_junc_idx]);
        else if (i == full_row_width-1)
            mp.buses.push_back(junctions[last_junc_idx]);
        else
            mp.buses.push_back(buses[last_bus_idx]);

        patch_idx++;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim