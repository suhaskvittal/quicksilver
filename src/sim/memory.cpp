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

static const MEMORY_MODULE::client_qubit_type UNITIALIZED{-1,-1};

MEMORY_MODULE::MEMORY_MODULE(double freq_ghz, size_t capacity)
    :CLOCKABLE(freq_ghz),
    contents(capacity, UNITIALIZED)
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
    if (req_it != request_buffer_.end() && serve_request(req_it))
        request_buffer_.erase(req_it);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MEMORY::request_buffer_type::iterator
MEMORY::find_ready_request()
{
    return request_buffer_.begin();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
MEMORY_MODULE::serve_request(request_buffer_type::iterator req_it)
{
    // 1. get qubit info (will be used later)
    auto& rq_qubit_info = req_it->client->qubits[req_it->requested_qubit];

    // 2. figure out how long it will take to "rotate" to
    //       the desired qubit and fetch the data
    
    // we can rotate the data left or right -- choose minimum latency
    size_t right_access_latency = std::distance(module_it->contents_.begin(), q_it);
    size_t left_access_latency = std::distance(q_it, module_it->contents_.end());

    size_t access_latency = std::min(left_access_latency, right_access_latency);
    size_t compute_cycles_for_ld = convert_cycles_between_frequencies(
                                                access_latency + LD_CYCLES, freq_ghz_, compute_->freq_ghz_);
    size_t compute_cycles_for_st = convert_cycles_between_frequencies(
                                                access_latency + LD_CYCLES + ST_CYCLES, freq_ghz_, compute_->freq_ghz_);

    // 3. check if routing space is available to serve the request
    auto& ev_qubit_info = compute_->select_victim_qubit();

    const auto& m_patch = compute_->patch(module_it->output_patch_idx);
    const auto& q_patch = compute_->patch(ev_qubit_info.memloc_info.patch_idx);

    if (!compute_->alloc_routing_space(m_patch, q_patch, compute_cycles_for_ld, compute_cycles_for_ld))
        return false;

    // 4. we can do the request:
    // 4.1. rotate the data to the desired qubit
    std::rotate(module_it->contents_.begin(), q_it, module_it->contents_.end());

    // 4.2. update the requested and evicted qubits' states
    rq_qubit_info.memloc_info.where = MEMINFO::LOCATION::COMPUTE;
    rq_qubit_info.memloc_info.t_free = compute_cycles_for_ld;

    ev_qubit_info.memloc_info.where = MEMINFO::LOCATION::MEMORY;
    ev_qubit_info.memloc_info.t_free = compute_cycles_for_st;

    // 4.3. update patch info
    q_patch.client_id = req_it->client_id;
    q_patch.qubit_id = req_it->requested_qubit;

    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim