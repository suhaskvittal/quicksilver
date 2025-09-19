/*
    author: Suhas Vittal
    date:   17 September 2025
*/

#include "sim/compute.h"
#include <functional>
#include "sim/memory.h"

//#define MEMORY_VERBOSE

namespace sim
{

extern COMPUTE* GL_CMP;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MEMORY_MODULE::MEMORY_MODULE(double freq_khz, size_t num_banks, size_t capacity_per_bank)
    :OPERABLE(freq_khz),
    num_banks_(num_banks),
    capacity_per_bank_(capacity_per_bank),
    banks_(num_banks, bank_type(capacity_per_bank))
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
MEMORY_MODULE::initiate_memory_access(QUBIT qubit)
{
    // create a new request:
    // first make sure that the qubit does not already have a request:
    auto req_it = find_request_for_qubit(qubit);
    if (req_it != request_buffer_.end())
        return;

    // otherwise, create a new request:
    request_type req{qubit};
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

    [[maybe_unused]] size_t bank_idx = std::distance(banks_.begin(), b_it);

    if (b_it->cycle_free > current_cycle())
    {
#if defined(MEMORY_VERBOSE)
        std::cout << "\tbank " << bank_idx << " is not free yet -- cycle_free = " << b_it->cycle_free << "\n";
#endif
        return false;
    }

    // first determine how long it will take to rotate the qubit to the head of the bank
    uint64_t left_rotation_cycles = std::distance(b_it->contents.begin(), q_it);
    uint64_t right_rotation_cycles = std::distance(q_it, b_it->contents.end());
    uint64_t rotation_cycles = std::min(left_rotation_cycles, right_rotation_cycles);

    uint64_t rotation_completion_cycle = std::max(current_cycle(), b_it->cycle_free) + rotation_cycles;

    // now, ask `GL_CMP` to route the memory access
    // convert `MSWAP_MEM_CYCLES` to compute cycles
    uint64_t earliest_start_time_ns = convert_cycles_to_ns(rotation_completion_cycle, OP_freq_khz);
    uint64_t mswap_time_ns = convert_cycles_to_ns(MSWAP_MEM_CYCLES, OP_freq_khz);

    // perform the memory swap and convert to compute cycles
    auto [victim_found, victim, access_time_ns] = 
            GL_CMP->route_memory_access(output_patch_idx_, req.qubit, earliest_start_time_ns, mswap_time_ns);

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

    // update when bank is free -- note that `access_time_ns` accounts for `rotation_completion_cycle`
    uint64_t mem_access_cycles = convert_ns_to_cycles(access_time_ns, OP_freq_khz);
    uint64_t cmp_access_cycles = convert_ns_to_cycles(access_time_ns, GL_CMP->OP_freq_khz);
    uint64_t mem_access_done_cycle = current_cycle() + mem_access_cycles;
    b_it->cycle_free = mem_access_done_cycle;

    // submit completion events
    MEMORY_EVENT_INFO mem_event_info;
    mem_event_info.bank_with_completed_request = bank_idx;
    OP_add_event_using_cycles(MEMORY_EVENT_TYPE::MEMORY_ACCESS_COMPLETED, mem_access_cycles + 1, mem_event_info);

    COMPUTE_EVENT_INFO cmp_event_info;
    cmp_event_info.mem_accessed_qubit = req.qubit;
    cmp_event_info.mem_victim_qubit = victim;
    GL_CMP->OP_add_event_using_cycles(COMPUTE_EVENT_TYPE::MEMORY_ACCESS_DONE, cmp_access_cycles + 1, cmp_event_info);

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