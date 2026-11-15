/*
 * EDPC's 1:3 data-to-ancilla patch layout. This models routing-patch
 * contention, not the EDPC edge-disjoint-path compiler or its gate schedule.
 * See Beverland, Kliuchnikov, and Schoute, PRX Quantum 3, 020342 (2022),
 * Section 3: data patches occupy even/even grid coordinates.
 * Contributor: OpenAI Codex (RoutingBase migration, 30 September 2026).
 * Contributor: OpenAI Codex (resource count return, 10 November 2026).
 */

#ifndef SIM_ROUTING_EDPC_LAYOUT_h
#define SIM_ROUTING_EDPC_LAYOUT_h

#include "sim/routing.h"

#include <unordered_map>
#include <vector>

namespace sim::routing
{

template <class Translator>
class EDPCLayout : public sim::RoutingBase<Translator>
{
public:
    explicit EDPCLayout(size_t data_patch_count);
    EDPCLayout(size_t data_patch_count, Translator);

    template <class T>
    void set_location(T, size_t data_index);

    template <class T, class U, class Callback>
    void for_each_resource_between(T, U, cycle_type from, cycle_type to, const Callback&) const;

    size_t patch_count() const { return resources_.size(); }
    size_t data_patch_count() const { return data_patch_count_; }

private:
    const size_t data_patch_count_;
    const size_t data_rows_;
    const size_t data_cols_;
    const size_t rows_;
    const size_t cols_;
    std::vector<Resource> resources_;
    std::unordered_map<id_type, size_t> locations_;

    bool _test_local_resource(id_type, cycle_type, cycle_type) const override;
    bool _test_resources_between(id_type, id_type, cycle_type, cycle_type) const override;
    void _lock_local_resource(id_type, cycle_type, cycle_type) override;
    size_t _lock_resources_between(id_type, id_type, cycle_type, cycle_type) override;

    bool is_data(size_t cell) const;
    std::vector<size_t> adjacent_ancillas(size_t data_cell, bool vertical_only) const;
    std::vector<size_t> route(id_type, id_type, cycle_type, cycle_type,
                              bool require_lockable = true) const;
    size_t local_resource(id_type, cycle_type, cycle_type) const;
};

} // namespace sim::routing

#include "edpc_layout.tpp"

#endif
