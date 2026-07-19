/*
 * author: Suhas Vittal
 * date:    11 July 2026
 * */

namespace rs
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class HistoryPtr, class ArrayType> void
add_idle_cycle_array(HistoryPtr& h, cycle_type current_cycle, const ArrayType& a)
{
    for (size_t i = 0; i < a.size(); i++)
        if (current_cycle >= a[i])
            h->add_idle(i, 1);
}

template <class HistoryPtr, class MapType> void
add_idle_cycle_map(HistoryPtr& h, cycle_type current_cycle, const MapType& m)
{
    for (auto [q,c] : m)
        if (current_cycle >= c)
            h->add_idle(q, 1);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace rs
