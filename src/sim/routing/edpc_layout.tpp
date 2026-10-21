/* Contributor: OpenAI Codex (RoutingBase migration, 30 September 2026). */

#include "sim/routing/multi_channel_bus.h" // boundary entry identifiers

#include <algorithm>
#include <cassert>
#include <cmath>
#include <queue>
#include <utility>

#define TEMPL_PARAMS template <class Translator>
#define TEMPL_CLASS EDPCLayout<Translator>

namespace sim::routing
{

TEMPL_PARAMS
TEMPL_CLASS::EDPCLayout(size_t n)
    :EDPCLayout(n, Translator{})
{}

TEMPL_PARAMS
TEMPL_CLASS::EDPCLayout(size_t n, Translator translator)
    :sim::RoutingBase<Translator>(std::move(translator)),
     data_patch_count_(n),
     data_rows_(std::max<size_t>(1, static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(n)))))),
     data_cols_(std::max<size_t>(1, (n + data_rows_ - 1) / data_rows_)),
     rows_(2 * data_rows_),
     cols_(2 * data_cols_),
     resources_(rows_ * cols_)
{
    locations_.reserve(n);
}

TEMPL_PARAMS template <class T> void
TEMPL_CLASS::set_location(T obj, size_t index)
{
    assert(index < data_patch_count_);
    const size_t row = index / data_cols_;
    const size_t col = index % data_cols_;
    locations_.emplace(this->translate(obj), (2 * row) * cols_ + 2 * col);
}

TEMPL_PARAMS template <class T, class U, class Callback> void
TEMPL_CLASS::for_each_resource_between(T src, U dst, cycle_type from, cycle_type to,
                                      const Callback& callback) const
{
    const id_type src_id = this->translate(src), dst_id = this->translate(dst);
    auto path = route(src_id, dst_id, from, to);
    // When no route is currently free, visit a geometric route so callers can
    // compute a later ready cycle instead of treating an empty path as ready.
    if (path.empty())
        path = route(src_id, dst_id, from, to, false);
    for (size_t cell : path)
        callback(resources_[cell]);
}

TEMPL_PARAMS bool
TEMPL_CLASS::is_data(size_t cell) const
{
    return (cell / cols_) % 2 == 0 && (cell % cols_) % 2 == 0;
}

TEMPL_PARAMS std::vector<size_t>
TEMPL_CLASS::adjacent_ancillas(size_t cell, bool vertical_only) const
{
    const size_t row = cell / cols_, col = cell % cols_;
    std::vector<size_t> out;
    if (row > 0) out.push_back(cell - cols_);
    if (row + 1 < rows_) out.push_back(cell + cols_);
    if (!vertical_only)
    {
        if (col > 0) out.push_back(cell - 1);
        if (col + 1 < cols_) out.push_back(cell + 1);
    }
    return out;
}

TEMPL_PARAMS size_t
TEMPL_CLASS::local_resource(id_type id, cycle_type from, cycle_type to) const
{
    const size_t cell = locations_.at(id);
    for (size_t next : adjacent_ancillas(cell, false))
        if (resources_[next].is_lockable(from, to)) return next;
    return resources_.size();
}

TEMPL_PARAMS bool
TEMPL_CLASS::_test_local_resource(id_type id, cycle_type from, cycle_type to) const
{
    return local_resource(id, from, to) != resources_.size();
}

TEMPL_PARAMS void
TEMPL_CLASS::_lock_local_resource(id_type id, cycle_type from, cycle_type to)
{
    const size_t cell = local_resource(id, from, to);
    assert(cell != resources_.size());
    resources_[cell].lock_for_time_interval(from, to);
}

TEMPL_PARAMS std::vector<size_t>
TEMPL_CLASS::route(id_type src, id_type dst, cycle_type from, cycle_type to,
                   bool require_lockable) const
{
    if (src == MCB_LEFT_ENTRY || src == MCB_RIGHT_ENTRY)
        std::swap(src, dst);
    assert(src >= 0 && src != dst);

    const size_t source = locations_.at(src);
    const bool boundary = dst == MCB_LEFT_ENTRY || dst == MCB_RIGHT_ENTRY;
    const size_t target = boundary ? resources_.size() : locations_.at(dst);
    const size_t absent = resources_.size();
    std::vector<size_t> parent(resources_.size(), absent);
    std::queue<size_t> frontier;

    // EDPC operator paths leave the source at its vertical boundary.
    for (size_t cell : adjacent_ancillas(source, true))
    {
        if (!require_lockable || resources_[cell].is_lockable(from, to))
        {
            parent[cell] = cell;
            frontier.push(cell);
        }
    }

    auto is_target = [&](size_t cell)
    {
        if (boundary)
            return cell % cols_ == (dst == MCB_LEFT_ENTRY ? 0 : cols_ - 1);
        // EDPC operator paths enter the target at its horizontal boundary.
        return cell / cols_ == target / cols_ &&
               (cell + 1 == target || target + 1 == cell);
    };

    while (!frontier.empty())
    {
        const size_t cell = frontier.front();
        frontier.pop();
        if (is_target(cell))
        {
            std::vector<size_t> path;
            for (size_t p = cell; ; p = parent[p])
            {
                path.push_back(p);
                if (parent[p] == p) break;
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        const size_t row = cell / cols_, col = cell % cols_;
        const size_t neighbors[4] = {
            row > 0 ? cell - cols_ : absent,
            row + 1 < rows_ ? cell + cols_ : absent,
            col > 0 ? cell - 1 : absent,
            col + 1 < cols_ ? cell + 1 : absent
        };
        for (size_t next : neighbors)
        {
            if (next == absent || is_data(next) || parent[next] != absent ||
                (require_lockable && !resources_[next].is_lockable(from, to))) continue;
            parent[next] = cell;
            frontier.push(next);
        }
    }
    return {};
}

TEMPL_PARAMS bool
TEMPL_CLASS::_test_resources_between(id_type src, id_type dst, cycle_type from, cycle_type to) const
{
    return !route(src, dst, from, to).empty();
}

TEMPL_PARAMS void
TEMPL_CLASS::_lock_resources_between(id_type src, id_type dst, cycle_type from, cycle_type to)
{
    auto path = route(src, dst, from, to);
    assert(!path.empty());
    for (size_t cell : path)
        resources_[cell].lock_for_time_interval(from, to);
}

} // namespace sim::routing

#undef TEMPL_PARAMS
#undef TEMPL_CLASS
