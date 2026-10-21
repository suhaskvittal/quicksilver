/*
 *  author: Suhas Vittal
 *  date:   10 March 2026
 *  contributor: OpenAI Codex (RoutingBase migration, 30 September 2026)
 * */

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <utility>

#define TEMPL_PARAMS template <class Translator>
#define TEMPL_CLASS  MultiChannelBus<Translator>

namespace sim
{
namespace routing
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS
TEMPL_CLASS::MultiChannelBus(size_t _num_channels, size_t num_resources_per_channel)
    :MultiChannelBus(_num_channels, num_resources_per_channel, Translator{})
{}

TEMPL_PARAMS
TEMPL_CLASS::MultiChannelBus(size_t _num_channels, size_t num_resources_per_channel, Translator translator)
    :sim::RoutingBase<Translator>(std::move(translator)),
    num_channels(_num_channels),
    channel_width(num_resources_per_channel),
    channels_(num_channels, channel_type(channel_width, Resource{}))
{
    location_map_.reserve(num_channels * channel_width * 2);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS template <class T> void
TEMPL_CLASS::set_location(T obj, int ch, int ro, int co)
{
    id_type x = this->translate(obj);
    assert(location_map_.find(x) == location_map_.end());
    location_map_[x] = coord_type{ch,ro,co};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS bool
TEMPL_CLASS::_test_local_resource(id_type obj, cycle_type a, cycle_type b) const
{
    return get_local_resource_ref(obj).is_lockable(a, b);
}

TEMPL_PARAMS bool
TEMPL_CLASS::_test_resources_between(id_type src, id_type dst, cycle_type a, cycle_type b) const
{
    bool can_lock{true};
    _for_each_resource_between(src, dst, [a, b, &can_lock] (const Resource& r) { can_lock &= r.is_lockable(a, b); });
    return can_lock;
}

TEMPL_PARAMS void
TEMPL_CLASS::_lock_local_resource(id_type obj, cycle_type a, cycle_type b)
{
    get_local_resource_ref(obj).lock_for_time_interval(a, b);
}

TEMPL_PARAMS void
TEMPL_CLASS::_lock_resources_between(id_type src, id_type dst, cycle_type a, cycle_type b)
{
    _for_each_resource_between(src, dst, [a, b] (Resource& r) { r.lock_for_time_interval(a, b); });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS template <class T, class U> void
TEMPL_CLASS::swap_locations_of(T x, U y)
{
    auto x_it = location_map_.find(this->translate(x)),
         y_it = location_map_.find(this->translate(y));
    assert(x_it != location_map_.end() && y_it != location_map_.end());
    std::swap(x_it->second, y_it->second);
}

TEMPL_PARAMS template <class T, class U> void
TEMPL_CLASS::replace(T out, U in)
{
    auto it = location_map_.find(this->translate(out));
    assert(it != location_map_.end());
    coord_type c = it->second;
    location_map_.erase(it);
    location_map_[this->translate(in)] = c;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////


TEMPL_PARAMS template <class T, class U> size_t
TEMPL_CLASS::patch_distance(T src, U dst) const
{
    const id_type src_id = this->translate(src),
                  dst_id = this->translate(dst);
    size_t d{0};
    _for_each_resource_between(src_id, dst_id, [&d] (const auto&) { d++; });
    // we also need to handle the case where `src` and `dst` are in different
    // channels:
    //
    // only when dst or src are not one of the channel entries
    if (src_id == MCB_LEFT_ENTRY || src_id == MCB_RIGHT_ENTRY || dst_id == MCB_LEFT_ENTRY || dst_id == MCB_RIGHT_ENTRY)
        return d;
    auto [s_ch, s_ro, s_co] = location_map_.at(src_id);
    auto [d_ch, d_ro, d_co] = location_map_.at(dst_id);
    if (s_ch != d_ch)
    {
        if (s_ch > d_ch)
            std::swap(s_ch, d_ch);
        // we need to adjust `d` for the channel difference.
        const size_t channels_traversed_exc = d_ch - s_ch - 1;  // channels traversed that are not the src/dst channel
        d += 3*channels_traversed_exc;  // these count for three (2 rows + 1 routing row per channel = height of 3)
        // for src and dst channels, only 2 extra patches per src and dst as we need  to exit via routing row + 
        // pass some other row . So four total
        // also subtract one for MCB_LEFT_ENTRY or MCB_RIGHT_ENTRY -- 3 total
        d += 3;
    }
    return d;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS template <class T, class U, class Callback> void
TEMPL_CLASS::for_each_resource_between(T src, U dst, cycle_type, cycle_type, const Callback& callback) const
{
    _for_each_resource_between(this->translate(src), this->translate(dst), callback);
}

TEMPL_PARAMS template <class T, class U, class Callback> void
TEMPL_CLASS::for_each_resource_between(T src, U dst, const Callback& callback) const
{
    for_each_resource_between(src, dst, 0, 0, callback);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS template <class T> auto&
TEMPL_CLASS::get_local_resource_ref(this auto& self, T obj)
{
    auto it = self.location_map_.find(self.translate(obj));
    if (it == self.location_map_.end())
        std::cerr << "MultiChannelBus::get_local_resource_ref(): no location registered for object" << _die{};
    auto [ch,ro,co] = it->second;
    return self.channels_[ch][co];
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS template <class Callback> void
TEMPL_CLASS::_for_each_resource_between(this auto& self, id_type src_id, id_type dst_id, const Callback& callback)
{
    assert(src_id != dst_id && (src_id >= 0 || dst_id >= 0));

    if (src_id == MCB_LEFT_ENTRY || src_id == MCB_RIGHT_ENTRY)
        return self._for_each_resource_between(dst_id, src_id, callback);

    auto [s_ch, s_ro, s_co] = self.location_map_.at(src_id);
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
    auto [d_ch, d_ro, d_co] = self.location_map_.at(dst_id);
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
                     right_entry_distance = 2*self.channel_width - s_co - d_co;
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
