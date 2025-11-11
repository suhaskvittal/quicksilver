/*
    author: Suhas Vittal
    date:   17 September 2025
*/

#include "clock.h"
#include "sim/compute.h"
#include <functional>
#include "sim/memory.h"

//#define MEMORY_VERBOSE

namespace sim
{

extern COMPUTE* GL_CMP;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uint64_t
MEMORY_MODULE::bank_type::rotate_to_location_and_store(std::vector<QUBIT>::iterator target_it, QUBIT stored)
{
    uint64_t left_rotation_cycles = std::distance(contents.begin(), target_it),
             right_rotation_cycles = std::distance(target_it, contents.end());
    uint64_t rotation_cycles = std::min(left_rotation_cycles, right_rotation_cycles);

    *target_it = stored;
    std::rotate(contents.begin(), target_it, contents.end());
    return rotation_cycles;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MEMORY_MODULE::MEMORY_MODULE(double freq_khz,
                            size_t num_banks,
                            size_t capacity_per_bank,
                            bool is_remote_module,
                            size_t epr_buffer_capacity,
                            uint64_t mean_epr_generation_cycle_time)
    :OPERABLE(freq_khz),
    num_banks_(num_banks),
    capacity_per_bank_(capacity_per_bank),
    banks_(num_banks, bank_type(capacity_per_bank)),
    is_remote_module_(is_remote_module)
{
    request_buffer_.reserve(128);

    // Create EPR generator for remote modules
    if (is_remote_module_)
    {
        // Convert mean generation cycle time to frequency for EPR_GENERATOR
        double epr_freq_khz = freq_khz / mean_epr_generation_cycle_time;
        epr_generator_ = new EPR_GENERATOR(epr_freq_khz, this, epr_buffer_capacity);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MEMORY_MODULE::~MEMORY_MODULE()
{
    if (epr_generator_ != nullptr)
        delete epr_generator_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MEMORY_MODULE::search_result_type
MEMORY_MODULE::find_qubit(QUBIT qubit)
{
    for (auto b_it = banks_.begin(); b_it != banks_.end(); b_it++)
    {
        auto q_it = std::find(b_it->contents.begin(), b_it->contents.end(), qubit);
        if (q_it != b_it->contents.end())
            return std::make_tuple(true, b_it, q_it);
    }

    return std::make_tuple(false, banks_.end(), banks_[0].contents.end());
}

MEMORY_MODULE::search_result_type
MEMORY_MODULE::find_uninitialized_qubit()
{
    return find_qubit(QUBIT{-1,-1});
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
MEMORY_MODULE::initiate_memory_access(inst_ptr inst, QUBIT requested, QUBIT victim, bool is_prefetch)
{
    // create a new request:
    // first make sure that the qubit does not already have a request:
    auto req_it = find_request_for_qubit(requested);
    if (req_it != request_buffer_.end())
    {
        s_num_prefetch_promoted_to_demand[requested.client_id] += (req_it->is_prefetch && !is_prefetch);
        req_it->is_prefetch &= is_prefetch;
        return;
    }

    if (is_prefetch)
        s_num_prefetch_requests[requested.client_id]++;

    // otherwise, create a new request:
    request_type req{inst, requested, victim, is_prefetch};
    if (!serve_memory_request(req))
        request_buffer_.push_back(req);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
MEMORY_MODULE::dump_contents()
{
    for (size_t i = 0; i < banks_.size(); i++)
    {
        std::cerr << "bank " << i << ":\n";
        for (size_t j = 0; j < banks_[i].contents.size(); j++)
            std::cerr << "\t" << j << " : " << banks_[i].contents[j] << "\n";
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
MEMORY_MODULE::OP_init()
{
    if (is_remote_module_)
        epr_generator_->OP_init();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
MEMORY_MODULE::OP_handle_event(event_type event)
{
#if defined(MEMORY_VERBOSE)
    std::cout << "[ MEMORY ] request queue contents:\n";
    for (const auto& req : request_buffer_)
    {
        auto [found, b_it, q_it] = this->find_qubit(req.qubit);
        size_t bank_idx = std::distance(banks_.begin(), b_it);
        std::cout << "\t\t" << req.qubit << ", bank = " << bank_idx << "\n";
    }
#endif

    if (event.id == MEMORY_EVENT_TYPE::MEMORY_ACCESS_COMPLETED || event.id == MEMORY_EVENT_TYPE::RETRY_MEMORY_ACCESS)
    {
        // then, retry all pending requests
        auto it = std::remove_if(request_buffer_.begin(), request_buffer_.end(),
                                [this] (const auto& req) { return this->serve_memory_request(req); });
        request_buffer_.erase(it, request_buffer_.end());
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
MEMORY_MODULE::serve_memory_request(const request_type& req)
{
#if defined(MEMORY_VERBOSE)
    if (is_coupled_memory_instruction(req.inst->type))
    {
        std::cout << "[ MEMORY ] initiated memswap between " 
                        << req.qubit << " and " << req.victim
                        << " @ cycle " << current_cycle() << "\n";
    }
    else if (req.inst->type == INSTRUCTION::TYPE::DLOAD)
    {
        std::cout << "[ MEMORY ] initiated decoupled load for " 
                        << req.qubit << " @ cycle " << current_cycle() << "\n";
    }
    else if (req.inst->type == INSTRUCTION::TYPE::DSTORE)
    {
        std::cout << "[ MEMORY ] initiated decoupled store for " 
                        << req.qubit << " @ cycle " << current_cycle() << "\n";
    }
#endif

    // check if this channel is free:
    if (cycle_free_ > current_cycle())
        return false;

    // if there aren't enough EPR pairs, then we also have to exit:
    const size_t epr_pairs_needed = is_coupled_memory_instruction(req.inst->type) ? 2 : 1;
    if (is_remote_module_ && epr_generator_->get_occupancy() < epr_pairs_needed)
        return false;

    // validate that we are not getting a decoupled load/store on a non-remote module
    if (!is_remote_module_ && !is_coupled_memory_instruction(req.inst->type))
        throw std::runtime_error("decoupled load/store on non-remote memory module");

    // find appropriate memory locatino:
    bool found;
    std::vector<bank_type>::iterator b_it;
    std::vector<QUBIT>::iterator q_it;

    if (is_coupled_memory_instruction(req.inst->type) || req.inst->type == INSTRUCTION::TYPE::DLOAD)
    {
        std::tie(found, b_it, q_it) = find_qubit(req.qubit);
        if (!found)
            throw std::runtime_error("qubit " + req.qubit.to_string() + " not found in memory");
    }
    else
    {
        std::tie(found, b_it, q_it) = find_uninitialized_qubit();
        if (!found)
            throw std::runtime_error("no uninitialized qubit found in memory for decoupled store");
    }

    [[maybe_unused]] size_t bank_idx = std::distance(banks_.begin(), b_it);

    // first determine how long it will take to rotate the qubit to the head of the bank
    uint64_t rotation_cycles;
    if (is_coupled_memory_instruction(req.inst->type))
        rotation_cycles = b_it->rotate_to_location_and_store(q_it, req.victim);
    else if (req.inst->type == INSTRUCTION::TYPE::DLOAD)
        rotation_cycles = b_it->rotate_to_location_and_store(q_it, QUBIT{-1,-1});
    else
        rotation_cycles = b_it->rotate_to_location_and_store(q_it, req.qubit);

    // perform routing + memory access operations:
    uint64_t post_routing_cycles{rotation_cycles};
    if (is_coupled_memory_instruction(req.inst->type))
        post_routing_cycles += MSWAP_CYCLES;
    else if (req.inst->type == INSTRUCTION::TYPE::DLOAD)
        post_routing_cycles += LOAD_CYCLES;
    else
        post_routing_cycles += STORE_CYCLES;

    uint64_t post_routing_time_ns = convert_cycles_to_ns(post_routing_cycles, OP_freq_khz);
    uint64_t access_time_ns;
    if (is_coupled_memory_instruction(req.inst->type))
        access_time_ns = GL_CMP->route_memory_access(output_patch_idx_, req.victim, cycle_free_);
    else if (req.inst->type == INSTRUCTION::TYPE::DLOAD)
        access_time_ns = 0;  // no routing required (storing in EPR pair)
    else
        access_time_ns = GL_CMP->route_memory_access(output_patch_idx_, req.qubit, cycle_free_);

    // final completion time is `GL_CURRENT_TIME_NS + access_time_ns + post_routing_time_ns`
    uint64_t completion_time_ns = GL_CURRENT_TIME_NS + access_time_ns + post_routing_time_ns;
    uint64_t mem_completion_cycle = convert_ns_to_cycles(completion_time_ns, OP_freq_khz),
             cmp_completion_cycle = convert_ns_to_cycles(completion_time_ns, GL_CMP->OP_freq_khz);
    
    // update when bank is free
    cycle_free_ = mem_completion_cycle;

    // submit completion events
    MEMORY_EVENT_INFO mem_event_info;
    OP_add_event_using_cycles(MEMORY_EVENT_TYPE::MEMORY_ACCESS_COMPLETED, 
                                mem_completion_cycle - current_cycle() + 1,
                                mem_event_info);

    COMPUTE_EVENT_INFO cmp_event_info;
    cmp_event_info.mem_accessed_qubit = req.qubit;
    cmp_event_info.mem_victim_qubit = req.victim;
    GL_CMP->OP_add_event_using_cycles(COMPUTE_EVENT_TYPE::MEMORY_ACCESS_DONE, 
                                        cmp_completion_cycle - GL_CMP->current_cycle(), 
                                        cmp_event_info);

    // update EPR generator -- if this is a decoupled store, then we get a loaded qubit popped off
    // the `decoupled_loads_` FIFO
    QUBIT dstore_loaded_qubit;
    if (is_remote_module_)
    {
        if (is_coupled_memory_instruction(req.inst->type))
            epr_generator_->consume_epr_pairs(epr_pairs_needed);
        else if (req.inst->type == INSTRUCTION::TYPE::DLOAD)
            epr_generator_->alloc_decoupled_load(req.qubit);
        else
            dstore_loaded_qubit = epr_generator_->free_decoupled_load();

        s_total_epr_buffer_occupancy_post_request += epr_generator_->get_occupancy();
    }

    // update qubit states:
    if (is_coupled_memory_instruction(req.inst->type))
        GL_CMP->update_state_after_memory_access(req.qubit, req.victim, cmp_completion_cycle, req.is_prefetch);
    else if (req.inst->type == INSTRUCTION::TYPE::DLOAD)
        GL_CMP->update_state_after_memory_access(req.qubit, QUBIT{-1,-1}, cmp_completion_cycle, false);
    else
        GL_CMP->update_state_after_memory_access(req.qubit, req.qubit, cmp_completion_cycle, req.is_prefetch);

#if defined(MEMORY_VERBOSE)
    std::cout << "\tserved memory request for qubit " << req.qubit << " in bank " << bank_idx 
                << "\n\t\tcompletion cycle = " << mem_access_done_cycle
                << "\n\t\tvictim = " << victim
                << "\n";
    std::cout << "\tbank " << bank_idx << " state:\n";
    for (size_t i = 0; i < banks_[bank_idx].contents.size(); i++)
        std::cout << "\t\t" << i << " : " << banks_[bank_idx].contents[i] << "\n";
#endif

    // update stats:
    s_memory_requests++;
    if (req.is_prefetch)
        s_memory_prefetch_requests++;

    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<MEMORY_MODULE::request_type>::iterator
MEMORY_MODULE::find_request_for_qubit(QUBIT qubit)
{
    return std::find_if(request_buffer_.begin(), request_buffer_.end(),
                        [qubit] (const auto& req) { return req.qubit == qubit; });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
mem_alloc_qubits_in_round_robin(std::vector<MEMORY_MODULE*> mem_modules, std::vector<QUBIT> qubits)
{
    // make sure there is enough space amongst all modules
    size_t total_capacity = std::transform_reduce(mem_modules.begin(), mem_modules.end(), 
                                            size_t{0},
                                            std::plus<size_t>(),
                                            [] (const auto* m) { return m->capacity_per_bank_ * m->num_banks_; });
    if (qubits.size() > total_capacity)
        throw std::runtime_error("not enough space in memory to allocate all qubits");

    size_t mem_idx{0};
    std::vector<size_t> mem_bank_idx(mem_modules.size(), 0);
    std::vector<bool> mem_done(mem_modules.size(), false);

    size_t i{0};
    while (i < qubits.size())
    {
        if (mem_done[mem_idx])
        {
            mem_idx = (mem_idx+1) % mem_modules.size();
            continue;
        }

        MEMORY_MODULE* m = mem_modules[mem_idx];
        size_t bank_idx = mem_bank_idx[mem_idx];
        
        size_t j{0};
        for (; j < m->num_banks_; j++)
        {
            auto& bank = m->banks_[bank_idx];
            auto q_it = std::find(bank.contents.begin(), bank.contents.end(), QUBIT{-1,-1});
            if (q_it != bank.contents.end())
            {
                *q_it = qubits[i];
                break;
            }

            bank_idx = (bank_idx+1) % m->num_banks_;
        }

        if (j == m->num_banks_)
        {
            mem_done[mem_idx] = true;
            mem_idx = (mem_idx+1) % mem_modules.size();
        }
        else
        {
            mem_bank_idx[mem_idx] = (bank_idx+1) % m->num_banks_;
            mem_idx = (mem_idx+1) % mem_modules.size();
            i++;
        }
    }
}
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim
