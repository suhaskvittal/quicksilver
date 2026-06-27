/*
 *  author: Suhas Vittal
 *  date:   11 March 2026
 * */

#include "perf_sim/memory_level.h"

#include <iostream>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MemoryLevel::MemoryLevel(std::string name, double freq_khz, size_t qubit_count, size_t n, size_t k, size_t d)
    :Operable(name, freq_khz),
    storage_physical_qubit_count(n),
    storage_logical_qubit_count(k),
    storage_code_distance(d),
    num_blocks(static_cast<size_t>( std::ceil(mean(qubit_count, k)) )),
    total_capacity(num_blocks * k)
{
    size_t num_blocks = std::ceil(mean(qubit_count, k));
    blocks_.resize(num_blocks);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MemoryAccessResult
MemoryLevel::do_load(Qubit* q)
{
    for (size_t i = 0; i < blocks_.size(); i++)
    {
        auto& s = blocks_[i];
        auto q_it = s.find(q);
        if (q_it != s.end())
        {
            auto result = load_impl(i, s, q);
            if (result.success)
                s.erase(q_it);
            return result;
        }
    }

    std::cerr << "MemoryLevel::do_load: could not find qubit " << *q
                << ", debug info:\n";
    dump_storage_info(std::cerr);
    exit(1);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MemoryAccessResult
MemoryLevel::do_store(Qubit* q)
{
    auto s_it = std::find_if(blocks_.begin(), blocks_.end(),
                            [k=storage_logical_qubit_count] (const auto& s) { return s.size() < k; });
    if (s_it == blocks_.end())
        std::cerr << "MemoryLevel::do_store: could not find empty location for store to qubit " << *q << _die{};
    size_t idx = std::distance(blocks_.begin(), s_it);
    auto result = store_impl(idx, *s_it, q);
    if (result.success)
        s_it->insert(q);
    return result;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MemoryAccessResult
MemoryLevel::do_coupled_load_store(Qubit* ld, Qubit* st)
{
    for (size_t i = 0; i < blocks_.size(); i++)
    {
        auto& s = blocks_[i];
        auto q_it = s.find(ld);
        if (q_it != s.end())
        {
            auto result = coupled_load_store_impl(i, s, ld, st);
            if (result.success)
            {
                s.erase(q_it);
                s.insert(st);
            }
            return result;
        }
    }

    std::cerr << "MemoryLevel::do_coupled_load_store: could not find qubit " << *ld
                << ", debug info:\n";
    dump_storage_info(std::cerr);
    exit(1);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
MemoryLevel::dump_storage_info(std::ostream& out) const
{
    out << name << "----------------------------";
    for (size_t i = 0; i < blocks_.size(); i++)
    {
        const auto& s = blocks_[i];
        out << "\ns" << i << " :";
        for (auto* q : s)
            out << " " << *q;
    }
    out << "\n";
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim
