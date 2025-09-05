/*
    author: Suhas Vittal
    date:   27 August 2025
*/

#include "sim/memory.h"

constexpr size_t LD_CYCLES = 2;
constexpr size_t ST_CYCLES = 1;

// #define QS_SIM_DEBUG

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

static const MEMORY_MODULE::client_qubit_type UNITIALIZED{-1,-1};

MEMORY_MODULE::MEMORY_MODULE(double freq_ghz, size_t num_banks, size_t capacity_per_bank)
    :CLOCKABLE(freq_ghz),
    num_banks_(num_banks),
    banks_(num_banks, bank_type(capacity_per_bank, UNITIALIZED))
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
MEMORY_MODULE::operate()
{
    // if there are no requests, we can exit
    if (request_buffer_.empty())
        return;

    for (auto req_it = request_buffer_.begin(); req_it != request_buffer_.end(); req_it++)
    {
        if (try_and_serve_request(req_it))
        {
            request_buffer_.erase(req_it);
            break;
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MEMORY_MODULE::qubit_lookup_result_type
MEMORY_MODULE::find_qubit(int8_t client_id, qubit_type qubit_id)
{
    for (auto b_it = banks_.begin(); b_it != banks_.end(); b_it++)
    {
        auto q_it = std::find_if(b_it->begin(), b_it->end(),
                                [client_id, qubit_id] (const auto& cq) { return cq.first == client_id && cq.second == qubit_id; });
        if (q_it != b_it->end())
            return std::make_pair(b_it, q_it);
    }

    return std::make_pair(banks_.end(), banks_[0].end());
}

MEMORY_MODULE::qubit_lookup_result_type
MEMORY_MODULE::find_uninitialized_qubit()
{
    return find_qubit(-1,-1);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
MEMORY_MODULE::try_and_serve_request(request_buffer_type::iterator req_it)
{
    // 1. get qubit info (will be used later)
    auto& rq_qubit_info = req_it->client->qubits[req_it->requested_qubit];

#if defined(QS_SIM_DEBUG)
    std::cout << "request: client = " << req_it->client_id+0 << ", qubit = " << req_it->requested_qubit << "\n";
#endif

    // 2. figure out how long it will take to "rotate" to
    //       the desired qubit and fetch the data
    auto [b_it, q_it] = find_qubit(req_it->client_id, req_it->requested_qubit);

    // we can rotate the data left or right -- choose minimum latency
    size_t right_access_latency = std::distance(b_it->begin(), q_it);
    size_t left_access_latency = std::distance(q_it, b_it->end());

    size_t access_latency = std::min(left_access_latency, right_access_latency);
    size_t compute_cycles_for_ld = convert_cycles_between_frequencies(
                                                access_latency + LD_CYCLES, freq_khz_, compute_->freq_khz_);
    size_t compute_cycles_for_st = convert_cycles_between_frequencies(
                                                access_latency + LD_CYCLES + ST_CYCLES, freq_khz_, compute_->freq_khz_);

    // 3. check if routing space is available to serve the request
    auto victim = compute_->select_victim_qubit();

    if (victim == nullptr)
        victim = compute_->select_random_victim_qubit(req_it->client_id, req_it->requested_qubit);


#if defined(QS_SIM_DEBUG)
    std::cout << "\tvictim: client = " << victim->memloc_info.client_id+0 
            << ", qubit = " << victim->memloc_info.qubit_id 
            << ", t_free = " << victim->memloc_info.t_free << "\n";

    if (victim->memloc_info.where == MEMINFO::LOCATION::MEMORY)
        std::cout << "\tvictim is in memory\n";
    else
        std::cout << "\tvictim is in compute\n";
#endif

    PATCH& m_patch = compute_->patches_[output_patch_idx];
    PATCH& q_patch = *compute_->find_patch_containing_qubit(victim->memloc_info.client_id, victim->memloc_info.qubit_id);

    if (!compute_->alloc_routing_space(m_patch, q_patch, compute_cycles_for_ld, compute_cycles_for_ld))
        return false;

    // 4. we can do the request:
    // 4.1. update the bank contents:
    *q_it = std::make_pair(victim->memloc_info.client_id, victim->memloc_info.qubit_id);

    // 4.2. rotate the data to the desired qubit
    std::rotate(b_it->begin(), q_it, b_it->end());

    // 4.3. update the requested and evicted qubits' states
    rq_qubit_info.memloc_info.where = MEMINFO::LOCATION::COMPUTE;
    rq_qubit_info.memloc_info.t_free = compute_->cycle_ + compute_cycles_for_ld;

    victim->memloc_info.where = MEMINFO::LOCATION::MEMORY;
    victim->memloc_info.t_free = compute_->cycle_ + compute_cycles_for_st;

    // 4.4. update patch info
    q_patch.client_id = req_it->client_id;
    q_patch.qubit_id = req_it->requested_qubit;


    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim