/*
 * author:  Suhas Vittal
 * date:    12 January 2026
 * */

#include "sim/storage.h"

#include <algorithm>
#include <cassert>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{
    
/*
 * Returns a generated name for the storage given its code parameters.
 * */
std::string _storage_name(size_t n, size_t k, size_t d);

/*
 * Fills up the storage by allocating qubits for each client in a round robin.
 * Only considers clients from `0 <= i < idx_upper_bound`
 * */
template <class ITER>
void _fill_up_storage_round_robin(ITER st_begin,
                                    ITER st_end, 
                                    std::vector<size_t>& qubits_allocated,
                                    const std::vector<std::vector<QUBIT*>>& qubits, 
                                    size_t idx_upper_bound);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

STORAGE::STORAGE(double freq_khz, size_t n, size_t k, size_t d, 
                size_t num_adapters, cycle_type _load_latency, cycle_type _store_latency)
    :OPERABLE(_storage_name(n,k,d), freq_khz),
    physical_qubit_count(n),
    logical_qubit_count(k),
    code_distance(d),
    load_latency(_load_latency),
    store_latency(_store_latency),
    contents_{},
    cycle_available_(num_adapters,0)
{
    contents_.reserve(k);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
STORAGE::contains(QUBIT* q) const
{
    return contents_.count(q) > 0;
}

void
STORAGE::insert(QUBIT* q)
{
    assert(contents_.size() < logical_qubit_count);
    contents_.insert(q);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

STORAGE::access_result_type
STORAGE::do_load(QUBIT* q)
{
    auto q_it = contents_.find(q);
    assert(q_it != contents_.end());

    auto result = do_memory_access(load_latency, ACCESS_TYPE::LOAD);
    result.critical_latency = load_latency;
    if (result.success)
        contents_.erase(q_it);
    return result;
}

STORAGE::access_result_type
STORAGE::do_store(QUBIT* q)
{
    assert(contents_.count(q) == 0);

    auto result = do_memory_access(store_latency, ACCESS_TYPE::STORE);
    result.critical_latency = 0;
    if (result.success)
    {
        contents_.insert(q);
        assert(contents_.size() <= logical_qubit_count);
    }
    return result;
}

STORAGE::access_result_type
STORAGE::do_coupled_load_store(QUBIT* ld, QUBIT* st)
{
    // additional data movement overhead to move out loaded qubit and move in stored qubit (surface code routing)
    const cycle_type ADDED_DATA_MOVEMENT_LATENCY = 2*code_distance;

    auto ld_it = contents_.find(ld);
    assert(ld_it != contents_.end() && contents_.count(st) == 0);

    auto result = do_memory_access(load_latency + store_latency + ADDED_DATA_MOVEMENT_LATENCY, 
                                    ACCESS_TYPE::COUPLED_LOAD_STORE);
    result.critical_latency = load_latency;
    if (result.success)
    {
        contents_.erase(ld_it);
        contents_.insert(st);
    }
    return result;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
STORAGE::has_free_adapter() const
{
    return std::any_of(cycle_available_.begin(), cycle_available_.end(),
                    [cc=current_cycle()] (cycle_type c) { return c <= cc; });
}

cycle_type
STORAGE::next_free_adapter_cycle() const
{
    return *std::min_element(cycle_available_.begin(), cycle_available_.end());
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
STORAGE::print_adapter_debug_info(std::ostream& out) const
{
    out << name << " adapters (free cycle delta):";
    for (cycle_type c : cycle_available_)
    {
        int64_t delta = static_cast<int64_t>(c) - static_cast<int64_t>(current_cycle());
        out << " " << delta;
    }
    out << "\n";
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

const STORAGE::backing_buffer_type&
STORAGE::contents() const
{
    return contents_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

long
STORAGE::operate()
{
    return 1;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

STORAGE::access_result_type
STORAGE::do_memory_access(cycle_type access_latency, ACCESS_TYPE type)
{
    auto adapter_it = std::find_if(cycle_available_.begin(), cycle_available_.end(),
                            [this] (cycle_type c) { return c <= current_cycle(); });
    if (adapter_it == cycle_available_.end())
        return access_result_type{};

    cycle_type total_latency{0};
    if (load_latency > 0)
    {
        cycle_type adapter_manip_latency = adapter_access(adapter_it, type);
        assert(adapter_manip_latency <= 2);
        total_latency = access_latency + adapter_manip_latency;
        *adapter_it = total_latency;
    }
    return access_result_type{ .success=true, .latency=total_latency, .storage_freq_khz=freq_khz };
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

cycle_type
STORAGE::adapter_access(std::vector<cycle_type>::iterator adapter_it, ACCESS_TYPE type)
{
    if (type == ACCESS_TYPE::STORE || load_latency == 0)
        return 0;  // any shift automorphisms can be done early
    else if (current_cycle() - 2 >= *adapter_it)
        return 0;
    else
        return 2 - (current_cycle() - *adapter_it);  // shift auts cannot be hidden (1 or 2 cycle latency)
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
storage_striped_initialization(const std::vector<STORAGE*>& storage_array,
                                const std::vector<std::vector<QUBIT*>>& qubits,
                                size_t num_active_clients)
{
    std::vector<size_t> qubits_allocated(qubits.size(), 0);

    auto st_begin = storage_array.begin(),
         st_end = storage_array.end();

    // first handle compute subsystem's local memory:
    _fill_up_storage_round_robin(st_begin, st_begin+1, qubits_allocated, qubits, num_active_clients);
    _fill_up_storage_round_robin(st_begin+1, st_end, qubits_allocated, qubits, qubits.size());

    // verify that all clients have been fully allocated
    bool any_clients_incomplete = false;
    for (size_t i = 0; i < qubits.size(); i++)
        any_clients_incomplete |= qubits_allocated[i] < qubits[i].size();
    if (any_clients_incomplete)
    {
        std::cerr << "storage_striped_initialization: storage_array was insufficient to allocate memory for"
                      " all clients:";
        for (size_t i = 0; i < qubits.size(); i++)
        {
            std::cerr << "\n\tclient " << i << " : allocated "
                        << qubits_allocated[i] << " of " << qubits[i].size();
        }
        std::cerr << _die{};
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

std::string
_storage_name(size_t n, size_t k, size_t d)
{
    return "[[" + std::to_string(n) + ", " + std::to_string(k) + ", " + std::to_string(d) + "]]";
}

template <class ITER> void
_fill_up_storage_round_robin(ITER st_begin,
                              ITER st_end,
                              std::vector<size_t>& qubits_allocated,
                              const std::vector<std::vector<QUBIT*>>& qubits,
                              size_t idx_upper_bound)
{
    /* 
     * Do the following:
     *      For each storage from `st_begin` to `st_end`, add one qubit
     *      from each client (up-to `idx_upper_bound`)
     * */

    bool done;
    do
    {
        bool any_qubit_inserted{false};
        for (auto it = st_begin; it != st_end; it++)
        {
            STORAGE* s = *it;
            for (size_t i = 0; i < idx_upper_bound; i++)
            {
                if (s->contents().size() == s->logical_qubit_count)
                    break;
                if (qubits_allocated[i] >= qubits[i].size())
                    continue;
                // get next qubit for client and place it into `s`
                QUBIT* q = qubits[i][qubits_allocated[i]];
                qubits_allocated[i]++;
                s->insert(q);

                any_qubit_inserted = true;
            }
        }

        done = !any_qubit_inserted;
    }
    while (!done);
}

}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim
