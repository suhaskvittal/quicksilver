/*
 *  author: Suhas Vittal
 *  date:   8 July 2026
 * */

#include "qs_reaction_sim/history.h"

#include "globals.h"

#include <algorithm>
#include <iostream>
#include <random>
#include <unordered_set>

namespace sim { extern std::mt19937_64 GL_RNG; }

namespace rs
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

constexpr std::string_view _event_to_string(HistoryEvent::Type);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

HistoryEvent*
HistoryEvent::init_idle(qubit_type q)
{
    return new HistoryEvent{
                            .type = HistoryEvent::Idle,
                            .qubits = {q}
                        };
}

HistoryEvent*
HistoryEvent::init_pauli_product_meas(std::initializer_list<qubit_type> qubits, cycle_type d, size_t w, cycle_type c)
{
    return new HistoryEvent{
                            .type = HistoryEvent::PauliProductMeas,
                            .cycle_available = c,
                            .duration = d,
                            .patch_count = w,
                            .qubits = std::vector<qubit_type>{qubits}
                        };
}

HistoryEvent*
HistoryEvent::init_conditional_basis_meas(inst_ptr owner, qubit_type q, cycle_type c)
{
    return new HistoryEvent{
                            .type = HistoryEvent::CondBasisMeas,
                            .cycle_available = c,
                            .owning_inst = owner,
                            .qubits = {q}
                        };
}

HistoryEvent*
HistoryEvent::init_known_basis_meas(qubit_type q, cycle_type c)
{
    return new HistoryEvent{
                            .type = HistoryEvent::KnownBasisMeas,
                            .cycle_available = c,
                            .qubits = {q},
                        };
}

HistoryEvent*
HistoryEvent::init_pauli_correction(qubit_type q, std::initializer_list<HistoryEvent*> driving_meas)
{
    // Every Pauli correction is Pauli-frame trackable and never blocks its
    // qubit's decoder: the decoder may advance past it out-of-order. The only
    // reaction-time block lives on the CondBasisMeas it may depend on, which
    // cannot resolve until its own predecessors have finished decoding.
    auto* e = new HistoryEvent{
                        .type = HistoryEvent::PauliCorrection,
                        .duration = 0,
                        .qubits = {q}
                    };
    // tie `e` with `driving_meas`
    for (auto* f : driving_meas)
    {
        e->predecessors.push_back(f);
        f->dependent.push_back(e);
    }
    return e;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

History::History(size_t n, size_t d, HistoryRole r, double eip)
    :max_program_qubits(n),
    code_distance(d),
    role(r),
    error_injection_probability(eip),
    next_ancilla_(static_cast<qubit_type>(n))
{
    front_layer_.reserve(n);
    back_layer_.reserve(n);
}

History::~History()
{
    // `front_layer` in this code segment is NOT the same `front_layer_`
    // in the class. A multi-qubit event fronts several keys, so dedup via `seen`.
    std::vector<HistoryEvent*> front_layer;
    std::unordered_set<HistoryEvent*> seen;
    for (const auto& [q, e] : front_layer_)
        if (e->predecessors.empty() && seen.insert(e).second)
            front_layer.push_back(e);

    while (event_count_ > 0 && !front_layer.empty())
    {
        std::vector<HistoryEvent*> next_front_layer;
        for (auto* e : front_layer)
        {
            if (e->predecessors.empty())
            {
                for (auto* f : e->dependent)
                {
                    auto e_it = std::find(f->predecessors.begin(), f->predecessors.end(), e);
                    f->predecessors.erase(e_it);
                    if (f->predecessors.empty())
                        next_front_layer.push_back(f);
                }
                event_count_--;
                delete e;
            }
            else
            {
                next_front_layer.push_back(e);
            }
        }
        front_layer = std::move(next_front_layer);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<qubit_type>
History::retire_front_event(qubit_type q)
{
    auto* e = front(q);
    if (e == nullptr)
        std::cerr << "History::retire_front_event: attempted to retire null event for qubit " << q << _die{};
    if (!e->is_fully_decoded())
        std::cerr << "History::retire_front_event: event is not fully decoded" << _die{};

    // A successorless idle that is still unresolved is the live tail of an
    // idling qubit: keep it at the front so it keeps absorbing idle cycles and
    // being re-decoded, instead of being retired off the front and orphaned
    // (which would let the next pushed event seize the vacated front slot ahead
    // of it). It is finalized later via the predecessor cascade in
    // `resolve_event` -- advancing to a successor if one has since arrived, or
    // resolving. A resolved (predecessor-less) tail must still retire, else the
    // qubit would wedge; measurements terminate their qubit, so they too retire
    // with no successor.
    if (e->type == HistoryEvent::Idle && e->dependent.empty() && !e->predecessors.empty())
        return {};

    // `e` has finished decoding: it leaves the front layer. Front promotion
    // is per-qubit, so advance every qubit `e` currently fronts to its
    // dependent. Resolution, by contrast, follows the full predecessor graph
    // and happens either now (predecessors already clear) or later, when the
    // last predecessor is removed by another event's `resolve_event`.
    advance_front_past(e);

    std::vector<qubit_type> freed;
    if (e->predecessors.empty())
        resolve_event(e, freed);
    return freed;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<qubit_type>
History::decode(cycle_type current_cycle, size_t max_volume)
{
    if (current_cycle < earliest_available_event_cycle_)
        return {};

    std::vector<qubit_type> freed;

    // AI-GENERATED
    //
    // Snapshot the fronted qubits (sorted, to keep the deterministic ascending-id
    // order the old `[0, max_qubits)` loop had) so retiring -- which mutates
    // `front_layer_` -- can't invalidate what we iterate. `front(q)` is re-read
    // each pass, so promotions/erasures from a shared event's retire are seen.
    std::vector<qubit_type> fronted;
    fronted.reserve(front_layer_.size());
    for (const auto& [q, e] : front_layer_)
        fronted.push_back(q);
    std::sort(fronted.begin(), fronted.end());

    // reset `earliest_available_event_cycle_`. We will set it in the loop
    earliest_available_event_cycle_ = std::numeric_limits<cycle_type>::max();

    for (qubit_type q : fronted)
    {
        size_t volume_decoded{0};
        while (volume_decoded < max_volume)
        {
            auto* e = front(q);
            if (e == nullptr)
                break;
            if (current_cycle < e->cycle_available)
                break;

            const auto volume_remaining = e->spacetime_volume() - e->volume_decoded;
            const auto volume_to_decode = std::min(volume_remaining, max_volume - volume_decoded);

            // Nothing left to decode for `e`. Two cases:
            //  --> this is a pauli correction, so advance decoder
            //  --> this is a blocked conditional basis measurement, so do not advance
            if (volume_to_decode == 0)
            {
                if (e->is_pauli_correction())
                {
                    auto f = retire_front_event(q);
                    freed.insert(freed.end(), f.begin(), f.end());
                    continue;
                }
                else
                {
                    break;
                }
            }

            e->volume_decoded += volume_to_decode;
            if (!verifier_is_using_accurate_decoder)
                e->erroneous_volume += volume_to_decode;

            if (e->is_fully_decoded())
            {
                auto f = retire_front_event(q);
                freed.insert(freed.end(), f.begin(), f.end());
            }
            volume_decoded += volume_to_decode;
        }

        // re-read `front(q)` to update `earliest_available_event_cycle_`
        auto* e = front(q);
        if (e != nullptr)
            earliest_available_event_cycle_ = std::min(earliest_available_event_cycle_, e->cycle_available);
    }

    return freed;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
History::advance_front_past(HistoryEvent* e)
{
    for (qubit_type x : e->qubits)
    {
        if (front(x) != e)
            continue;
        auto f_it = std::find_if(e->dependent.begin(), e->dependent.end(),
                            [x] (const auto* f)
                            {
                                auto x_it = std::find(f->qubits.begin(), f->qubits.end(), x);
                                return x_it != f->qubits.end();
                            });
        set_front(x, (f_it == e->dependent.end()) ? nullptr : *f_it);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
History::add_idle(qubit_type q, cycle_type duration)
{
    // Check back layer for `q`. If it is an idle, conditional basis
    // measurement, or known basis measurement, then we can increment
    // the duration.
    auto* e = back(q);
    const bool cannot_append_idle = (e == nullptr)
                                    || (e->type == HistoryEvent::PauliProductMeas)
                                    || e->is_pauli_correction();
    if (cannot_append_idle)
    {
        HistoryEvent* idle = HistoryEvent::init_idle(q);
        idle->duration = duration;
        if (e != nullptr)
        {
            e->dependent.push_back(idle);
            idle->predecessors.push_back(e);
        }
        if (front(q) == nullptr)
            set_front(q, idle);
        back_layer_[q] = idle;
        event_count_++;
    }
    else
    {
        e->duration += duration;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

History::event_add_result_type
History::add_events_for_instructions(inst_ptr inst, cycle_type current_cycle)
{
    auto* uop = (inst->uop_count() > 0) ? inst->current_uop() : inst;
    if (is_software_instruction(uop->type) || uop->type == Instruction::Type::H)
        return {};
    // only care about CX, S, and T
    if (is_cx_like_instruction(uop->type))
        return add_cx_like_instruction(uop, current_cycle);
    if (is_s_like_instruction(uop->type))
        return add_s_like_instruction(uop, current_cycle);
    if (is_t_like_instruction(uop->type))
        return add_t_like_instruction(uop, current_cycle);
    std::cerr << "History::add_events_for_instructions: unexpected instruction: " << *uop << _die{};
    return {};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
History::push_back_event(HistoryEvent* e)
{
    std::unordered_set<HistoryEvent*> visited;
    for (qubit_type q : e->qubits)
    {
        auto* b = back(q);
        if (b != nullptr && visited.count(b) == 0)
        {
            b->dependent.push_back(e);
            e->predecessors.push_back(b);
            visited.insert(b);
        }
        back_layer_[q] = e;
        if (front(q) == nullptr)
            set_front(q, e);
    }
    event_count_++;
    // Only non-idle events are pushed here (idles go through `add_idle`).
    non_idle_event_count_++;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
History::resolve_event(HistoryEvent* e, std::vector<qubit_type>& freed)
{
    if (!e->predecessors.empty())
        std::cerr << "History::resolve_event: attempted to resolve event with pending predecessors" << _die{};

    // AI-GENERATED
    //
    // A conditional basis measurement is the one blocking event: only now that
    // its predecessors have all resolved is the decode of its window final. What
    // that means for the owning instruction depends on the role:
    //  --> Reaction     : its basis is now known, so it may react/retire.
    //  --> Verification : the slow decoder has caught up and checked the fast
    //                     decoder's guess. (Whether that guess was erroneous is
    //                     determined elsewhere; here we only mark it verified.)
    if (e->type == HistoryEvent::CondBasisMeas)
    {
        if (role == HistoryRole::Reaction)
            e->owning_inst->rx.retireable = true;
        else
            e->owning_inst->rx.verified = true;
    }

    // randomly inject errors (probability of error scales with spacetime volume)
    if (role == HistoryRole::Verification)
    {
        static std::uniform_real_distribution<double> FPR(0.0, 1.0);
        const double p = FPR(sim::GL_RNG),
                     pth = error_injection_probability*e->erroneous_volume;
        if (p < pth)
        {
            inject_error(e);
            s_errors_injected++;
        }
    }

    // `e` may still be a front here when reached directly by the cascade rather
    // than through `retire_front_event` -- e.g. a blocked CBM whose predecessors
    // just cleared, or a zero-volume correction resolved the instant its last
    // predecessor did. Advancing here keeps `front_layer_` from dangling.
    advance_front_past(e);

    // Unlink `e` from each dependent. Removing `e` may make a dependent that has
    // already finished decoding (and left the front, or a zero-volume
    // correction) resolvable -- cascade into it. The gate `is_fully_decoded()`
    // ensures we never free a dependent that is still mid-decode at the front.
    for (auto* f : e->dependent)
    {
        auto e_it = std::find(f->predecessors.begin(), f->predecessors.end(), e);
        if (e_it != f->predecessors.end())
            f->predecessors.erase(e_it);
        if (f->is_fully_decoded() && f->predecessors.empty())
            resolve_event(f, freed);
    }

    for (qubit_type q : e->qubits)
    {
        if (back(q) == e)
            back_layer_.erase(q);
    }

    // An ancilla is consumed by a single non-Clifford. Its measurement (known- or
    // conditional-basis) is the last event on that ancilla; as it resolves the
    // ancilla drops out of the layer maps. With the ever-increasing ancilla
    // pointer ids are never reused, so there is nothing to return to a pool.
    // Only conditional-basis (T-like) ancillas are lifetime-tracked by the Driver
    // (`anc_available_cycle_`); a known-basis ancilla is never entered there.
    // Report only the former in `freed` so the caller drops exactly the entries
    // it holds.
    if (e->type == HistoryEvent::CondBasisMeas)
        freed.push_back(e->qubits.front());

    if (e->type != HistoryEvent::Idle)
        non_idle_event_count_--;
    delete e;
    event_count_--;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
History::inject_error(HistoryEvent* e)
{
    // find all affected T gates:
    std::vector<HistoryEvent*> dfs{e};
    std::unordered_set<HistoryEvent*> visited;
    while (dfs.size() > 0)
    {
        auto* x = dfs.back();
        dfs.pop_back();
        if (visited.count(x))
            continue;
        visited.insert(x);
        if (x->type == HistoryEvent::CondBasisMeas && x->owning_inst != nullptr)
        {
            x->owning_inst->rx.erroneous = true;
        }
        else
        {
            for (auto* y : x->dependent)
                dfs.push_back(y);
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

qubit_type
History::get_ancilla()
{
    // Ever-increasing pointer: hand out a fresh id and never reuse it.
    return next_ancilla_++;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void 
History::set_front(qubit_type q, HistoryEvent* e)
{
    if (e == nullptr) 
    {
        front_layer_.erase(q);
    }
    else
    {
        front_layer_[q] = e;
        earliest_available_event_cycle_ = std::min(earliest_available_event_cycle_, e->cycle_available);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

History::event_add_result_type
History::add_cx_like_instruction(inst_ptr inst, cycle_type c)
{
    qubit_type q0 = inst->qubits[0],
                q1 = inst->qubits[1],
                a = get_ancilla();
    // we need to do two PPMs + one known basis measurement
    auto* ppm1 = HistoryEvent::init_pauli_product_meas({q0, a}, code_distance, inst->rx.routing_space_consumed, c+code_distance),
        * ppm2 = HistoryEvent::init_pauli_product_meas({q1, a}, code_distance, inst->rx.routing_space_consumed, c+2*code_distance),
        * ma = HistoryEvent::init_known_basis_meas(a, c+2*code_distance+1);
    auto* cq0 = HistoryEvent::init_pauli_correction(q0, {ppm1, ma}),
        * cq1 = HistoryEvent::init_pauli_correction(q1, {ppm2});


    push_back_events({ppm1, ppm2, ma, cq0, cq1});
    // The ancilla is measured in a known basis, so it is deallocated immediately
    // in practice and needs no idle-cycle tracking -- do not report it.
    return {};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

History::event_add_result_type
History::add_s_like_instruction(inst_ptr inst, cycle_type c)
{
    qubit_type q = inst->qubits[0],
                a = get_ancilla();
    auto* ppm = HistoryEvent::init_pauli_product_meas({q, a}, code_distance, 2, c+code_distance),
        * ma = HistoryEvent::init_known_basis_meas(a, c+code_distance+1);
    auto* cq = HistoryEvent::init_pauli_correction(q, {ma});
    push_back_events({ppm, ma, cq});
    return {};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

History::event_add_result_type
History::add_t_like_instruction(inst_ptr inst, cycle_type c)
{
    const cycle_type meas_y_latency = code_distance/2 + 2;
    const cycle_type expected_meas_latency = (meas_y_latency+1) / 2;

    qubit_type q = inst->qubits[0],
                a = get_ancilla();
    auto* ppm = HistoryEvent::init_pauli_product_meas({q, a}, code_distance, inst->rx.routing_space_consumed, c+code_distance);
    auto* ma = HistoryEvent::init_conditional_basis_meas(inst, a, c+code_distance+expected_meas_latency);
    auto* cq = HistoryEvent::init_pauli_correction(q, {ma});
    push_back_events({ppm, ma, cq});
    return {.ancilla = {a}};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::ostream&
operator<<(std::ostream& ostrm, const HistoryEvent& e)
{
    ostrm << _event_to_string(e.type)
            << " [";
    for (size_t i = 0; i < e.qubits.size(); i++)
    {
        if (i > 0)
            ostrm << ", ";
        ostrm << e.qubits[i];
    }
    ostrm << "] volume decoded = " << e.volume_decoded << "/" << e.spacetime_volume()
        << ", pred = " << e.predecessors.size() << ", dep = " << e.dependent.size();
    return ostrm;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

constexpr std::string_view
_event_to_string(HistoryEvent::Type t)
{
    constexpr std::string_view STR[]
    {
        "Idle", "PauliProductMeas", "CondBasisMeas", "KnownBasisMeas", "PauliCorrection"
    };
    return STR[static_cast<size_t>(t)];
}

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace rs
