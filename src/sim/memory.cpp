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
    is_remote_module_(is_remote_module),
    epr_buffer_capacity_(epr_buffer_capacity),
    mean_epr_generation_cycle_time_(mean_epr_generation_cycle_time)
{
    request_buffer_.reserve(128);
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
        OP_add_event_using_cycles(MEMORY_EVENT_TYPE::REMOTE_EPR_PAIR_GENERATED, mean_epr_generation_cycle_time_, MEMORY_EVENT_INFO{});
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

    if (event.id == MEMORY_EVENT_TYPE::MEMORY_ACCESS_COMPLETED)
    {
        size_t free_bank_idx = event.info.bank_with_completed_request;
        // find a request mapping to this bank and issue it
        auto req_it = find_request_for_bank(free_bank_idx, request_buffer_.begin());

#if defined(MEMORY_VERBOSE)
        std::cout << "\tmemory access completed for bank " << free_bank_idx << ", cycle = " << current_cycle() << "\n";
        if (req_it != request_buffer_.end())
            std::cout << "\tfound request for qubit " << req_it->qubit << "\n"; 
        else
            std::cout << "\tno request found for bank " << free_bank_idx << "\n";
#endif

        while (req_it != request_buffer_.end() && !serve_memory_request(*req_it))
        {
#if defined(MEMORY_VERBOSE)
            if (req_it != request_buffer_.end())
                std::cout << "\tfound request for qubit " << req_it->qubit << "\n"; 
            else
                std::cout << "\tno request found for bank " << free_bank_idx << "\n";
#endif
            req_it = find_request_for_bank(free_bank_idx, req_it+1);
        }

        if (req_it != request_buffer_.end())
            request_buffer_.erase(req_it);
    }
    else if (event.id == MEMORY_EVENT_TYPE::COMPUTE_COMPLETED_INST)
    {
        // then, retry all pending requests
        auto it = std::remove_if(request_buffer_.begin(), request_buffer_.end(),
                                [this] (const auto& req) { return this->serve_memory_request(req); });
        request_buffer_.erase(it, request_buffer_.end());
    }
    else if (event.id == MEMORY_EVENT_TYPE::REMOTE_EPR_PAIR_GENERATED)
    {
        epr_buffer_occu_++;
        if (epr_buffer_occu_ < epr_buffer_capacity_)
            OP_add_event_using_cycles(MEMORY_EVENT_TYPE::REMOTE_EPR_PAIR_GENERATED, mean_epr_generation_cycle_time_, MEMORY_EVENT_INFO{});

        // retry memory requests:
        if (epr_buffer_occu_ >= 2)
        {
            auto it = std::remove_if(request_buffer_.begin(), request_buffer_.end(),
                                    [this] (const auto& req) { return this->serve_memory_request(req); });
            request_buffer_.erase(it, request_buffer_.end());
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
MEMORY_MODULE::serve_memory_request(const request_type& req)
{
    constexpr uint64_t MSWAP_MEM_CYCLES = 3;  // note that these are memory cycles

#if defined(MEMORY_VERBOSE)
    std::cout << "[ MEMORY ] initiated memory access to qubit " << req.qubit << " cycle " << current_cycle() << "\n";
#endif

    auto [found, b_it, q_it] = find_qubit(req.qubit);
    if (!found)
        throw std::runtime_error("qubit " + req.qubit.to_string() + " not found in memory");

    if (b_it->cycle_free > current_cycle())
        return false;

    // if there aren't enough EPR pairs, then we also have to exit:
    if (is_remote_module_ && epr_buffer_occu_ < 2)
        return false;

    [[maybe_unused]] size_t bank_idx = std::distance(banks_.begin(), b_it);

    // first determine how long it will take to rotate the qubit to the head of the bank
    uint64_t left_rotation_cycles = std::distance(b_it->contents.begin(), q_it);
    uint64_t right_rotation_cycles = std::distance(q_it, b_it->contents.end());
    uint64_t rotation_cycles = std::min(left_rotation_cycles, right_rotation_cycles);

    uint64_t post_routing_cycles = rotation_cycles + MSWAP_MEM_CYCLES;
    uint64_t post_routing_time_ns = convert_cycles_to_ns(post_routing_cycles, OP_freq_khz);

    // perform the memory swap and convert to compute cycles
    auto [victim_found, victim, access_time_ns] = GL_CMP->route_memory_access(
                                                                output_patch_idx_,
                                                                req.qubit,
                                                                req.is_prefetch,
                                                                req.victim,
                                                                post_routing_time_ns);

    // Since victim is now required, victim_found should always be true
    // This check is kept for safety but should never fail
    if (!victim_found)
    {
#if defined(MEMORY_VERBOSE)
        std::cout << "\tfailed to find victim for qubit " << req.qubit << "\n";
#endif
        return false;
    }

    // update memory:
    *q_it = victim;
    // complete the rotation now that we have assigned `*q_it`
    std::rotate(b_it->contents.begin(), q_it, b_it->contents.end());

    // final completion time is `GL_CURRENT_TIME_NS + access_time_ns + post_routing_time_ns`
    uint64_t completion_time_ns = GL_CURRENT_TIME_NS + access_time_ns + post_routing_time_ns;
    uint64_t mem_completion_cycle = convert_ns_to_cycles(completion_time_ns, OP_freq_khz);
    uint64_t cmp_completion_cycle = convert_ns_to_cycles(completion_time_ns, GL_CMP->OP_freq_khz);
    
    // update when bank is free
    b_it->cycle_free = mem_completion_cycle;

    // submit completion events
    MEMORY_EVENT_INFO mem_event_info;
    mem_event_info.bank_with_completed_request = bank_idx;
    OP_add_event_using_cycles(MEMORY_EVENT_TYPE::MEMORY_ACCESS_COMPLETED, mem_completion_cycle - current_cycle() + 1, mem_event_info);

    COMPUTE_EVENT_INFO cmp_event_info;
    cmp_event_info.mem_accessed_qubit = req.qubit;
    cmp_event_info.mem_victim_qubit = victim;
    GL_CMP->OP_add_event_using_cycles(COMPUTE_EVENT_TYPE::MEMORY_ACCESS_DONE, cmp_completion_cycle - GL_CMP->current_cycle(), cmp_event_info);

    if (is_remote_module_)
    {
        // only submit a generation event if the buffer is full, as this clears the buffer
        if (epr_buffer_occu_ == epr_buffer_capacity_)
            OP_add_event_using_cycles(MEMORY_EVENT_TYPE::REMOTE_EPR_PAIR_GENERATED, mean_epr_generation_cycle_time_, MEMORY_EVENT_INFO{});

        epr_buffer_occu_ -= 2;
        s_total_epr_buffer_occupancy_post_request += epr_buffer_occu_;
    }
    s_memory_requests++;
    if (req.is_prefetch)
        s_memory_prefetch_requests++;

#if defined(MEMORY_VERBOSE)
    std::cout << "\tserved memory request for qubit " << req.qubit << " in bank " << bank_idx 
                << "\n\t\tcompletion cycle = " << mem_access_done_cycle
                << "\n\t\tvictim = " << victim
                << "\n";
    std::cout << "\tbank " << bank_idx << " state:\n";
    for (size_t i = 0; i < banks_[bank_idx].contents.size(); i++)
        std::cout << "\t\t" << i << " : " << banks_[bank_idx].contents[i] << "\n";
#endif

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

std::vector<MEMORY_MODULE::request_type>::iterator
MEMORY_MODULE::find_request_for_bank(size_t idx, std::vector<request_type>::iterator search_from_req_it)
{
    auto target_b_it = banks_.begin() + idx;
    return std::find_if(search_from_req_it, request_buffer_.end(),
                        [this, target_b_it] (const auto& req)
                        {
                            auto [found, b_it, q_it] = this->find_qubit(req.qubit);
                            return found && (b_it == target_b_it);
                        });
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
