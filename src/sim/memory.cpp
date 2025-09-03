/*
    author: Suhas Vittal
    date:   27 August 2025
*/

#include "sim/memory.h"

constexpr size_t LD_CYCLES = 2;
constexpr size_t ST_CYCLES = 1;

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MEMORY_MODULE::MEMORY_MODULE(double freq_ghz, size_t capacity)
    :CLOCKABLE(freq_ghz),
    contents_(capacity, UNITIALIZED)
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MEMORY::MEMORY(const std::vector<MEMORY_MODULE*>& modules, COMPUTE* compute)
    :modules_(modules),
    compute_(compute)
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
MEMORY::operate()
{
    // if there are no requests, we can exit
    if (request_buffer_.empty())
        return;

    request_buffer_type::iterator req_it = find_ready_request();
    while (req_it != request_buffer_.end())
    {
        req_it->completed = serve_request(req_it);
        req_it = find_ready_request(std::next(req_it));
    }

    // remove all completed requests
    auto it = std::remove_if(request_buffer_.begin(), request_buffer_.end(),
                             [](const request_type& req) { return req.completed; });
    request_buffer_.erase(it, request_buffer_.end());
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MEMORY::request_buffer_type::iterator
MEMORY::find_ready_request()
{
    return find_ready_request(request_buffer_.begin());
}

MEMORY::request_buffer_type::iterator
MEMORY::find_ready_request(request_buffer_type::iterator from)
{
    return std::find_if(from, request_buffer_.end(),
                        [](const request_type& req) { return req.completed; });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
MEMORY::serve_request(request_buffer_type::iterator req_it)
{
    // 1. figure out which module contains the requested qubit
    int8_t client_id = req_it->client_id;
    qubit_type requested_qubit = req_it->requested_qubit;

    // get their memory info as well (useful later)
    auto& rq_qubit_info = req_it->client->qubits[requested_qubit];

    auto [module_it, q_it] = search_for_qubit(client_id, requested_qubit);

    // 2. figure out how long it will take to "rotate" to
    //       the desired qubit and fetch the data
    
    // we can rotate the data left or right -- choose minimum latency
    size_t right_access_latency = std::distance(module_it->contents_.begin(), q_it);
    size_t left_access_latency = std::distance(q_it, module_it->contents_.end());

    size_t access_latency = std::min(left_access_latency, right_access_latency);
    size_t compute_cycles_for_ld = convert_cycles_between_frequencies(
                                                access_latency + LD_CYCLES, module_it->freq_ghz_, compute_->freq_ghz_);
    size_t compute_cycles_for_st = convert_cycles_between_frequencies(
                                                access_latency + LD_CYCLES + ST_CYCLES, module_it->freq_ghz_, compute_->freq_ghz_);

    // 3. check if routing space is available to serve the request
    auto& ev_qubit_info = compute_->select_victim_qubit();

    const auto& m_patch = compute_->patch(module_it->patch_idx);
    const auto& q_patch = compute_->patch(ev_qubit_info.memloc_info.patch_idx);

    if (!compute_->alloc_routing_space(m_patch, q_patch, compute_cycles_for_ld, compute_cycles_for_ld))
        return false;

    // 4. we can do the request:
    // 4.1. rotate the data to the desired qubit
    std::rotate(module_it->contents_.begin(), q_it, module_it->contents_.end());

    // 4.2. update the requested and evicted qubits' states
    rq_qubit_info.memloc_info.where = MEMINFO::LOCATION::COMPUTE;
    rq_qubit_info.memloc_info.t_free = compute_cycles_for_ld;
    rq_qubit_info.memloc_info.patch_idx = ev_qubit_info.memloc_info.patch_idx;

    ev_qubit_info.memloc_info.where = MEMINFO::LOCATION::MEMORY;
    ev_qubit_info.memloc_info.t_free = compute_cycles_for_st;

    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MEMORY::qubit_search_result_type
MEMORY::search_for_qubit(int8_t client_id, qubit_type qubit_id)
{
    auto target = std::make_pair(client_id, qubit_id);
    for (auto it = modules_.begin(); it != modules_.end(); it++)
    {
        auto begin = (*it)->contents_.begin(),
             end = (*it)->contents_.end();
        auto q_it = std::find(begin, end, target);
        if (q_it != end)
            return {it, q_it};
    }
    return {};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim