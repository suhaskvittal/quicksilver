/*
 *  author: Suhas Vittal
 *  date:   10 March 2026
 * */

#include <algorithm>
#include <cassert>
#include <type_traits>

#define TEMPL_PARAMS    template <class IMPL>
#define TEMPL_CLASS     class MULTI_CHANNEL_BUS<IMPL>

namespace sim
{
namespace routing
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS
TEMPL_CLASS::MULTI_CHANNEL_BUS(size_t _num_channels, size_t num_resources_per_channel)
    :num_channels(_num_channels),
    channel_width(num_resources_per_channel),
    channels_(num_channels, channel_type(channel_width, RESOURCE{}))
{
    location_map_.reserve(num_channels * channel_width * 2);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS template <class T> void
TEMPL_CLASS::set_location(T obj, int ch, int ro, int co)
{
    id_type x = translate(obj);
    assert(location_map_.find(x) == location_map_.end());
    location_map_[x] = coord_type{ch,ro,co};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS template <class T> bool
TEMPL_CLASS::test_and_lock_local_resource(T obj, cycle_type a, cycle_type b)
{
    auto& r = _get_local_resource_ref(obj);
    bool can_lock = r.is_lockable(a, b);
    if (can_lock)
        r.lock_for_time_interval(a, b);
    return can_lock;
}

TEMPL_PARAMS template <class T, class U> bool
TEMPL_CLASS::test_and_lock_resources_between(T src, U dst, cycle_type a, cycle_type b)
{
    bool can_lock{true};
    _for_each_resource_between(src, dst, [a, b, &can_lock] (const RESOURCE& r) { can_lock &= r.is_lockable(a, b); });
    if (!can_lock)
        return false;
    _for_each_resource_between(src, dst, [a, b] (RESOURCE& r) { r.lock_for_time_interval(a, b); });
    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS template <class T, class U> void
TEMPL_PARAMS::swap_locations_of(T x, U y)
{
    auto x_it = location_map_.find(translate(x)),
         y_it = location_map_.find(translate(y));
    assert(x_it != location_map_.end() && y_it != location_map_.end());
    std::swap(x_it->second, y_it->second);
}

TEMPL_PARAMS template <class T, class U> void
TEMPL_PARAMS::replace(T out, U in)
{
    auto it = location_map_.find(translate(out));
    assert(it != location_map_.end());
    coord_type c = it->second;
    location_map_.erase(it);
    location_map_[in] = c;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////


TEMPL_PARAMS template <class T> const RESOURCE&
TEMPL_CLASS::get_local_resource_ref(T obj) const
{
    return _get_local_resource_ref(obj);
}

TEMPL_PARAMS template <class T, class U> const RESOURCE&
TEMPL_CLASS::for_each_resource_between(T src, U dst, const CALLBACK& callback) const
{
    return _for_each_resource_between(src, dst, callback);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS template <class T> typename TEMPL_CLASS::id_type
TEMPL_CLASS::translate(T obj) const
{
    if constexpr (std::is_integral<T>::value)
        return static_cast<id_type>(obj);
    else
        return static_cast<const IMPL*>(this)->translate(obj);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS template <class T> auto&
TEMPL_CLASS::_get_local_resource_of(this auto& self, T obj)
{
    id_type x = self.translate(obj);
    auto it = self.location_map_.find(x);
    assert(it != self.location_map_.end());
    auto [ch,ro,co] = it->second;
    return channels_[ch][co];
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS template <class T, class U, const CALLBACK&> void
TEMPL_CLASS::_for_each_resource_between(this auto& self, T src, U dst, const CALLBACK& callback)
{
    id_type src_id = self.translate(src),
            dst_id = self.translate(dst);
    assert(src_id != dst_id);

    if (src_id == MCB_LEFT_ENTRY || src_id == MCB_RIGHT_ENTRY)
        return self._for_each_resource_between(dst, src, callback);

    const auto [s_ch, s_ro, s_co] = self.location_map_.at(src_id);
    if (dst_id == MCB_LEFT_ENTRY || dst_id == MCB_RIGHT_ENTRY)
    {
        // same-channel, take up entire left/right side
        auto begin = self.channels_[s_ch].begin(),
             end = self.channels_[s_ch].end();
        auto src_it = begin + s_co;
        if (dst_id == MCB_LEFT_ENTRY)
            std::for_each(begin, src_it+1, callback);
        else
            std::for_each(src_it, end, callback);
        return;
    }

    // check if `dst` is in the same channel as src:
    const auto [d_ch, d_ro, d_co] = self.location_map_.at(dst_id);
    if (s_ch == d_ch)
    {
        if (s_co > d_co)
            std::swap(s_co, d_co);
        auto begin = self.channels_[s_ch].begin() + s_co;
        auto end = self.channels_[s_ch].begin() + d_co;
        std::for_each(begin, end+1, callback);
    }
    else
    {
        // so, we need to choose either `MCB_LEFT_ENTRY` or `MCB_RIGHT_ENTRY` to connect both channels.
        // choose based off of distance
        const size_t left_entry_distance = s_co + d_co,
              size_t right_entry_distance = 2*channel_width - s_co - d_co;
        auto s_ch_begin = self.channels_[s_ch].begin(),
             s_ch_end = self.channels_[s_ch].end(),
             d_ch_begin = self.channels_[d_ch].begin(),
             d_ch_end = self.channels_[d_ch].end();
        if (left_entry_distance < right_entry_distance)
        {
            std::for_each(s_ch_begin, s_ch_begin+s_co+1, callback);
            std::for_each(d_ch_begin, d_ch_begin+d_co+1, callback);
        }
        else
        {
            std::for_each(s_ch_begin+s_co, s_ch_end, callback);
            std::for_each(d_ch_begin+d_co, d_ch_end, callback);
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace routing
} // namespace sim

#undef TEMPL_PARAMS
#undef TEMPL_CLASS
