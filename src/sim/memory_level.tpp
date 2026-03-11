/*
 *  author: Suhas Vittal
 *  date:   11 March 2026
 * */

#include <iostream>

#define TEMPL_PARAMS  template <class IMPL>
#define TEMPL_CLASS   MEMORY_LEVEL<IMPL>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS
TEMPL_CLASS::MEMORY_LEVEL(std::string_view name, double freq_khz, size_t qubit_count, size_t n, size_t k, size_t d)
    :OPERABLE(name, freq_khz),
    storage_physical_qubit_count(n),
    storage_logical_qubit_count(k),
    storage_code_distance(d),
    total_capacity(static_cast<size_t>( std::ceil(mean(qubit_count, k)) * k ))
{
    size_t num_blocks = std::ceil(mean(qubit_count, k));
    blocks_.resize(num_blocks);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS MEMORY_ACCESS_RESULT
TEMPL_CLASS::do_load(QUBIT* q)
{
    for (size_t i = 0; i < blocks_.size(); i++)
    {
        auto& s = blocks_[i];
        auto q_it = s.find(q);
        if (q_it != s.end())
        {
            auto result = static_cast<IMPL*>(this)->load_impl(i, s, q);
            if (result.success)
                s.erase(q_it);
            return result;
        }
    }

    std::cerr << "MEMORY_LEVEL::do_load: could not find qubit " << *q
                << ", debug info:\n";
    dump_storage_info(std::cerr);
    exit(1);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS MEMORY_ACCESS_RESULT
TEMPL_CLASS::do_store(QUBIT* q)
{
    auto s_it = std::find_if(blocks_.begin(), blocks_.end(), 
                            [k=storage_logical_qubit_count] (const auto& s) { return s.size() < k; });
    if (s_it == blocks_.end())
        std::cerr << "MEMORY_LEVEL::do_store: could not find empty location for store to qubit " << *q << _die{};
    size_t idx = std::distance(blocks_.begin(), s_it);
    auto result = static_cast<IMPL*>(this)->store_impl(idx, *s_it, q);
    if (result.success)
        s_it->insert(q);
    return result;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS MEMORY_ACCESS_RESULT
TEMPL_CLASS::do_coupled_load_store(QUBIT* ld, QUBIT* st)
{
    for (size_t i = 0; i < blocks_.size(); i++)
    {
        auto& s = blocks_[i];
        auto q_it = s.find(ld);
        if (q_it != s.end())
        {
            auto result = static_cast<IMPL*>(this)->coupled_load_store_impl(i, s, ld, st);
            if (result.success)
            {
                s.erase(q_it);
                s.insert(st);
            }
            return result;
        }
    }

    std::cerr << "MEMORY_LEVEL::do_coupled_load_store: could not find qubit " << *ld
                << ", debug info:\n";
    dump_storage_info(std::cerr);
    exit(1);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS void
TEMPL_CLASS::dump_storage_info(std::ostream& out) const
{
    out << name << "----------------------------";
    for (size_t i = 0; i < blocks_.size(); i++)
    {
        const auto& s = blocks_[i];
        out << "\ns" << i << " :";
        for (auto* q : s)
            out << " " << q;
    }
    out << "\n";
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS cycle_type
TEMPL_CLASS::get_next_ready_cycle_for_load(QUBIT* q) const
{
    return static_cast<IMPL*>(this)->get_next_ready_cycle_for_load_impl(q);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#undef TEMPL_PARAMS
#undef TEMPL_CLASS
