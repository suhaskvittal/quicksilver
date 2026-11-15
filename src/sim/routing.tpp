/*
 *  author: OpenAI Codex
 *  date:   30 September 2026
 *  contributor: OpenAI Codex (resource count return, 10 November 2026)
 * */

#include <type_traits>
#include <utility>

namespace sim
{

template <class Translator>
RoutingSpace<Translator>::RoutingSpace() = default;

template <class Translator>
RoutingSpace<Translator>::RoutingSpace(Translator translator)
    :translator_(std::move(translator))
{}

template <class Translator>
RoutingSpace<Translator>::~RoutingSpace() = default;

template <class Translator>
template <class T>
routing::id_type
RoutingSpace<Translator>::translate(T obj) const
{
    if constexpr (std::is_integral_v<T>)
        return static_cast<routing::id_type>(obj);
    else
        return translator_.translate(obj);
}

template <class Translator>
template <class T>
bool
RoutingSpace<Translator>::test_local_resource(T obj, cycle_type from, cycle_type to) const
{
    return _test_local_resource(translate(obj), from, to);
}

template <class Translator>
template <class T, class U>
bool
RoutingSpace<Translator>::test_resources_between(T src, U dst, cycle_type from, cycle_type to) const
{
    return _test_resources_between(translate(src), translate(dst), from, to);
}

template <class Translator>
template <class T>
void
RoutingSpace<Translator>::lock_local_resource(T obj, cycle_type from, cycle_type to)
{
    _lock_local_resource(translate(obj), from, to);
}

template <class Translator>
template <class T, class U>
size_t
RoutingSpace<Translator>::lock_resources_between(T src, U dst, cycle_type from, cycle_type to)
{
    return _lock_resources_between(translate(src), translate(dst), from, to);
}

} // namespace sim
