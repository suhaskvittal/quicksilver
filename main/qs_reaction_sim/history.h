/*
 *  author: Suhas Vittal
 *  date:   6 July 2026
 * */

#ifndef RS_HISTORY_h
#define RS_HISTORY_h

#include "qs_reaction_sim/common.h"

#include "globals.h"
#include "instruction.h"

#include <initializer_list>
#include <unordered_map>
#include <vector>

namespace rs
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * AI-GENERATED
 *
 * A `History` plays one of two roles, distinguished only by what resolving a
 * conditional-basis measurement signals to the owning instruction:
 *  --> `Reaction`     : the fast decoder. Resolving a CBM means its basis is now
 *                       known, so the instruction may react/retire (`rx.retireable`).
 *  --> `Verification` : the slow decoder. The reaction already happened; resolving
 *                       a CBM means the slow decoder has caught up and checked the
 *                       fast decoder's guess (`rx.verified`).
 * All other mechanics -- events, the predecessor DAG, window accounting -- are
 * identical between the two roles.
 * */
enum class HistoryRole { Reaction, Verification };

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct HistoryEvent
{
    enum class Type
    {
        Idle,               // qubit idles for n cycles
        PauliProductMeas,
        CondBasisMeas,      // the sole blocking event: gates its qubit's decoder until its predecessors decode
        KnownBasisMeas,
        PauliCorrection,    // Pauli-frame-trackable correction; never blocks, decodable out-of-order
    };

    using enum Type;

    using inst_ptr = Instruction*;

    Type type;
    /*
     * An event can only be decoded once the current cycle exceeds `cycle_available`
     * */
    cycle_type cycle_available{};
    /*
     * `duration` and `patch_count` dictate the amount of volume that must be decoded.
     * See `spacetime_volume()` below.
     * */
    cycle_type duration{1};
    size_t     patch_count{1};
    size_t     volume_decoded{0};

    inst_ptr owning_inst{nullptr};

    std::vector<qubit_type> qubits;

    std::vector<HistoryEvent*> predecessors{},
                               dependent{};

    /*
     * Initialization functions:
     * */
    static HistoryEvent* init_idle(qubit_type);
    static HistoryEvent* init_pauli_product_meas(std::initializer_list<qubit_type>, 
                                                    cycle_type d, 
                                                    size_t width, 
                                                    cycle_type cycle_available);
    static HistoryEvent* init_conditional_basis_meas(inst_ptr, qubit_type, cycle_type cycle_available);
    static HistoryEvent* init_known_basis_meas(qubit_type, cycle_type cycle_available);
    static HistoryEvent* init_pauli_correction(qubit_type, std::initializer_list<HistoryEvent*>);

    size_t spacetime_volume() const { return duration * patch_count; }
    bool is_pauli_correction() const { return type == PauliCorrection; }

    bool
    is_fully_decoded() const
    { 
        return volume_decoded == spacetime_volume()
                && (type != HistoryEvent::CondBasisMeas || predecessors.empty());
    }
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * `History` tracks pending syndromes for each qubit.
 * Note that `History` does not explicitly track idle
 * cycles. We only store cycles where Pauli-product
 * measurements, or other important operations, occur.
 * */

class History
{
public:
    using inst_ptr = Instruction*;

    struct event_add_result_type
    {
        /*
         * List of ancilla used in non-clifford operations.
         * We need to track these for idle cycles.
         * */
        std::vector<qubit_type> ancilla;
    };

    const size_t max_program_qubits,
                 code_distance;
    const HistoryRole role;
    /*
     * AI-GENERATED
     *
     * Per-window probability that the fast decoder mis-decoded, used only by a
     * `Verification`-role history. Zero for a `Reaction` history.
     * */
    const double error_injection_probability;
private:
    /*
     * `front_layer_` maps each qubit to its oldest undecoded event; `back_layer_`
     * maps each qubit to its youngest event. A multi-qubit event may appear under
     * several keys. A missing key means the qubit has no event -- `front`/`back`
     * return nullptr. These are maps rather than fixed vectors because ancilla ids
     * come from an ever-increasing pointer (never reused), so the id space is
     * unbounded but sparse.
     * */
    std::unordered_map<qubit_type, HistoryEvent*> front_layer_,
                                                  back_layer_;
    size_t event_count_{0};
    /*
     * AI-GENERATED
     *
     * Count of non-idle events currently live. `only_contains_idles()` tests this
     * against zero, so it cannot be fooled by a real event hidden behind a front
     * idle on the same qubit.
     * */
    size_t non_idle_event_count_{0};

    /*
     * Ancilla allocator: an ever-increasing counter starting just past the program
     * qubits. Ids are never reused, so freeing an ancilla is just dropping it from
     * the layer maps.
     * */
    qubit_type next_ancilla_;
public:
    History(size_t max_program_qubits, size_t code_distance, HistoryRole,
                double error_injection_probability = 0.0);
    ~History();

    HistoryEvent* front(qubit_type q) const
    { auto it = front_layer_.find(q); return it == front_layer_.end() ? nullptr : it->second; }
    HistoryEvent* back(qubit_type q) const
    { auto it = back_layer_.find(q); return it == back_layer_.end() ? nullptr : it->second; }

    /*
     * AI-GENERATED
     *
     * Retires the fully-decoded front event on `q`, cascading resolution
     * through the predecessor graph. Returns the conditional-basis (T-like)
     * ancillas freed during this call -- the only ancillas the Driver
     * lifetime-tracks -- so it can drop their `anc_available_cycle_` entries.
     * */
    std::vector<qubit_type> retire_front_event(qubit_type);

    /*
     * AI-GENERATED
     *
     * Decodes syndrome volume for every qubit's front layer, giving each qubit a
     * budget of `max_windows` windows this call. Fully-decoded events are retired
     * (and out-of-order Pauli corrections skipped past), cascading resolution
     * through the predecessor graph. Returns the
     * conditional-basis ancillas freed across all retires -- the only ancillas the
     * fast-path Driver lifetime-tracks. The slow-path caller may discard them.
     * */
    std::vector<qubit_type> decode(cycle_type current_cycle, size_t max_windows);

    void add_idle(qubit_type, cycle_type duration);

    /*
     * Auto-generates decoding events for given instruction. Events are made
     * available at a future time depending on the instruction and current cycle.
     * See `HistoryEvent::cycle_available`
     * */
    event_add_result_type add_events_for_instructions(inst_ptr, cycle_type current_cycle);
    
    size_t event_count() const { return event_count_; }

    /*
     * AI-GENERATED
     *
     * True when every live event is an idle -- i.e. all real syndrome events have
     * decoded/resolved. RAD uses this as the "wrong-path resolution finished"
     * signal once the main program is stalled.
     * */
    bool only_contains_idles() const { return non_idle_event_count_ == 0; }
private:
    qubit_type get_ancilla();

    /*
     * AI-GENERATED
     *
     * Sets the front event for `q`, erasing the entry when `e` is null so
     * `front_layer_` only ever holds live fronts (and `front` can treat a missing
     * key as nullptr).
     * */
    void set_front(qubit_type q, HistoryEvent* e)
    {
        if (e == nullptr) front_layer_.erase(q);
        else              front_layer_[q] = e;
    }

    void push_back_event(HistoryEvent*);
    void push_back_events(std::initializer_list<HistoryEvent*> arr) { for (auto* e : arr) push_back_event(e); }

    /*
     * AI-GENERATED
     *
     * `advance_front_past` moves `front_layer_` forward for every qubit for
     * which `e` is currently the front, promoting the appropriate dependent
     * (or nullptr). It is idempotent: a no-op for an event no longer fronting
     * any qubit.
     *
     * `resolve_event` finalizes an event whose predecessors have all cleared:
     * it fires the CBM `retireable` signal, unlinks itself from its
     * dependents, cascades into any dependent that has now become resolvable
     * (fully decoded with no remaining predecessors -- which also covers
     * zero-volume corrections), clears the back layer, and frees the event.
     * Any conditional-basis ancilla returned to the pool (by this event or by
     * a cascaded resolution) is appended to `freed`.
     * */
    void advance_front_past(HistoryEvent*);
    void resolve_event(HistoryEvent*, std::vector<qubit_type>& freed);

    /*
     * AI-GENERATED
     *
     * Taints the T gates a fast-decoder error on `e`'s window would corrupt.
     * If `e` is itself a conditional-basis measurement (a T gate's measurement),
     * only its owning instruction is flagged. Otherwise the error is on data/
     * ancilla volume that flows downstream, so every CBM reachable along
     * `dependent` edges has its owning instruction flagged. Only called for a
     * `Verification`-role history.
     * */
    void inject_error(HistoryEvent*);

    event_add_result_type add_cx_like_instruction(inst_ptr, cycle_type);
    event_add_result_type add_s_like_instruction(inst_ptr, cycle_type);
    event_add_result_type add_t_like_instruction(inst_ptr, cycle_type);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::ostream& operator<<(std::ostream&, const HistoryEvent&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace rs

#endif
