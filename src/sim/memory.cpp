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
extern EPR_GENERATOR* GL_EPR;

extern bool GL_IMPL_CACHEABLE_STORES;

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
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MEMORY_MODULE::~MEMORY_MODULE()
{
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

std::vector<QUBIT>
MEMORY_MODULE::get_all_stored_qubits() const
{
    std::vector<QUBIT> all_qubits;

    for (const auto& bank : banks_)
    {
        for (const auto& qubit : bank.contents)
        {
            if (qubit.qubit_id >= 0)  // Valid qubit (not {-1,-1})
            {
                all_qubits.push_back(qubit);
            }
        }
    }

    return all_qubits;
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
        throw std::runtime_error("qubit " + requested.to_string() + " already has a pending request");
    }

    if (is_prefetch)
        s_num_prefetch_requests[requested.client_id]++;

    // otherwise, create a new request:
    request_type req{inst, requested, victim, is_prefetch};
    if (!request_buffer_.empty() || !serve_memory_request(req))
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

bool
MEMORY_MODULE::can_serve_request() const
{
    return cycle_free_ <= current_cycle();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
MEMORY_MODULE::OP_init()
{
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
MEMORY_MODULE::OP_handle_event(event_type event)
{
    if (current_cycle() < cycle_free_ || request_buffer_.empty())
        return;

#if defined(MEMORY_VERBOSE)
    std::cout << "[ MEMORY patch = " << output_patch_idx_ << " ] request queue contents:\n";
    for (const auto& req : request_buffer_)
    {
        auto [found, b_it, q_it] = this->find_qubit(req.qubit);
        size_t bank_idx = std::distance(banks_.begin(), b_it);
        std::cout << "\t\t" << *req.inst << ", bank = " << bank_idx << "\n";
    }

    if (is_remote_module_)
    {
        std::cout << "\tEPR cached qubits:";
        for (const auto& q : GL_EPR->get_cached_qubits())
            std::cout << " " << q;
        std::cout << ", epr buffer occu: " << GL_EPR->get_occupancy() 
            << ", has capacity = " << GL_EPR->has_capacity()
            << "\n";
    }
#endif

    if (event.id == MEMORY_EVENT_TYPE::MEMORY_ACCESS_COMPLETED || event.id == MEMORY_EVENT_TYPE::RETRY_MEMORY_ACCESS)
    {
        auto it = request_buffer_.end();

        if (it == request_buffer_.end())
        {
            it = std::find_if(request_buffer_.begin(), request_buffer_.end(),
                                [this] (const auto& req) { return serve_memory_request(req); });
        }
        if (it != request_buffer_.end())
            request_buffer_.erase(it);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
MEMORY_MODULE::serve_memory_request(const request_type& req)
{
#if defined(MEMORY_VERBOSE)
    std::cout << "[ MEMORY ] initiated " << *req.inst 
                    << " @ cycle " << current_cycle() 
                    << " (GL_CMP cycle = " << GL_CMP->current_cycle() << ")\n";
#endif

    // both of these variables determine where the memory access goes
    bool load_is_cached{false};
    bool will_cache_store{false};

    if (GL_IMPL_CACHEABLE_STORES && is_remote_module_)
    {
        load_is_cached = GL_EPR->qubit_is_cached(req.qubit);
        will_cache_store = is_cacheable_memory_instruction(req.inst->type) && GL_EPR->store_is_cacheable();

#if defined(MEMORY_VERBOSE)
        std::cout << "\tload_is_cached = " << load_is_cached 
                    << ", will_cache_store = " << will_cache_store << "\n";
#endif

        if (load_is_cached && will_cache_store)
        {
            cache_store_into_cached_load(req);
            return true;
        }
    }

    // check if this channel is free:
    if (cycle_free_ > current_cycle())
    {
#if defined(MEMORY_VERBOSE)
        std::cout << "\tchannel not free (cycle_free = " << cycle_free_ << "), retrying later\n";
#endif
        return false;
    }

    // if there aren't enough EPR pairs, then we also have to exit:
    size_t epr_pairs_needed{2 - load_is_cached - will_cache_store};
    if (is_remote_module_ && GL_EPR->get_occupancy() < epr_pairs_needed)
    {
#if defined(MEMORY_VERBOSE)
        std::cout << "\tnot enough EPR pairs (need " << epr_pairs_needed 
            << ", have " << GL_EPR->get_occupancy() << "), retrying later\n";
#endif
        return false;
    }


    // first determine how long it will take to rotate the qubit to the head of the bank
    uint64_t rotation_cycles;

    // find appropriate memory locatino:
    if (load_is_cached)
    {
        auto [found, b_it, q_it] = find_uninitialized_qubit();            
        if (!found)
        {
            // we are foced to cache this store:
            cache_store_into_cached_load(req);
            return true;
        }
        rotation_cycles = b_it->rotate_to_location_and_store(q_it, req.victim);
    }
    else
    {
        auto [found, b_it, q_it] = find_qubit(req.qubit);
        if (!found)
            throw std::runtime_error("qubit " + req.qubit.to_string() + " not found in memory");

        rotation_cycles = b_it->rotate_to_location_and_store(q_it, will_cache_store ? QUBIT{-1,-1} : req.victim);
    }

    // perform routing + memory access operations:
    uint64_t post_routing_cycles{rotation_cycles};
    if (!load_is_cached)
        post_routing_cycles += LOAD_CYCLES;
    if (!will_cache_store)
        post_routing_cycles += STORE_CYCLES;

    uint64_t post_routing_time_ns = convert_cycles_to_ns(post_routing_cycles, OP_freq_khz);
    uint64_t access_time_ns = GL_CMP->route_memory_access(output_patch_idx_, req.victim, cycle_free_);

    // final completion time is `GL_CURRENT_TIME_NS + access_time_ns + post_routing_time_ns`
    uint64_t completion_time_ns = GL_CURRENT_TIME_NS + access_time_ns + post_routing_time_ns;
    uint64_t mem_completion_cycle = convert_ns_to_cycles(completion_time_ns, OP_freq_khz),
             cmp_completion_cycle = convert_ns_to_cycles(completion_time_ns, GL_CMP->OP_freq_khz);

    if (load_is_cached)  // compute only cares about how long the load takes
        cmp_completion_cycle = GL_CMP->current_cycle() + 2;

#if defined(MEMORY_VERBOSE)
    std::cout << "[ MEMORY ] mem access for " << req.qubit << " <--> " << req.victim 
                << " will complete @ cycle = " << mem_completion_cycle
                << " (cmp cycle = " << cmp_completion_cycle << ")\n"
                << "\tpost_routing_cycles = " << post_routing_cycles
                << ", access_cycles = " << convert_ns_to_cycles(access_time_ns, OP_freq_khz)
                << "\n";
#endif
    
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

    // `cmp_cycles_from_now` corresponds to the completion of the load
    uint64_t cmp_cycles_from_now = (cmp_completion_cycle - GL_CMP->current_cycle());
    GL_CMP->OP_add_event_using_cycles(COMPUTE_EVENT_TYPE::MEMORY_ACCESS_DONE, cmp_cycles_from_now, cmp_event_info);

    // update EPR generator:
    if (is_remote_module_)
    {
        GL_EPR->consume_epr_pairs(epr_pairs_needed);
        if (will_cache_store)
            GL_EPR->cache_qubit(req.victim);
        else if (load_is_cached)
            GL_EPR->remove_qubit(req.qubit);

        size_t occu = GL_EPR->get_occupancy();
        s_total_epr_buffer_occupancy_post_request += occu;
    }

    // update qubit states:
    GL_CMP->update_state_after_memory_access(req.qubit, req.victim, cmp_completion_cycle, req.is_prefetch);

    // update stats:
    s_memory_requests++;
    if (req.is_prefetch)
        s_memory_prefetch_requests++;

    if (load_is_cached)
        s_loads_from_cache++;
    if (will_cache_store)
        s_cached_stores++;

    s_total_memory_access_latency_in_compute_cycles += cmp_cycles_from_now;

    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
MEMORY_MODULE::cache_store_into_cached_load(const request_type& req)
{
    // both are cached -- complete immediately as this is at EPR generator
    GL_EPR->swap_qubit_for(req.qubit, req.victim);

    // send memory event:
    MEMORY_EVENT_INFO mem_event_info;
    OP_add_event(MEMORY_EVENT_TYPE::MEMORY_ACCESS_COMPLETED, 0, mem_event_info);

    // send compute event:
    COMPUTE_EVENT_INFO cmp_event_info;
    cmp_event_info.mem_accessed_qubit = req.qubit;
    cmp_event_info.mem_victim_qubit = req.victim;
    GL_CMP->OP_add_event_using_cycles(COMPUTE_EVENT_TYPE::MEMORY_ACCESS_DONE, 2, cmp_event_info);

    // update status in events:
    GL_CMP->update_state_after_memory_access(req.qubit, req.victim, 2, req.is_prefetch);

    // update stats:
    s_memory_requests++;
    if (req.is_prefetch)
        s_memory_prefetch_requests++;
    s_cached_stores++;
    s_loads_from_cache++;
    s_forwards++;

    s_total_memory_access_latency_in_compute_cycles += 2;
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
