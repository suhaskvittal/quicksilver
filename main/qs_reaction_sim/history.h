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
#include <vector>

namespace rs
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct HistoryEvent
{
    enum class Type
    {
        Idle,               // qubit idles for n cycles
        PauliProductMeas,
        CondBasisMeas,      // these are the only "blocking" events
        KnownBasisMeas,
        BlockingCorrection,     // correction that gates its qubit's decoder (depends on a CBM, e.g. T)
        NonblockingCorrection,  // Pauli-frame-trackable correction, decodable out-of-order (e.g. CX, S)
    };

    using enum Type;

    using inst_ptr = Instruction*;

    Type type;
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
    static HistoryEvent* init_pauli_product_meas(std::initializer_list<qubit_type>, cycle_type d, size_t width);
    static HistoryEvent* init_conditional_basis_meas(inst_ptr, qubit_type);
    static HistoryEvent* init_known_basis_meas(qubit_type);
    static HistoryEvent* init_pauli_correction(qubit_type, std::initializer_list<HistoryEvent*>);

    size_t spacetime_volume() const { return duration * patch_count; }

    bool is_pauli_correction() const { return type == BlockingCorrection || type == NonblockingCorrection; }

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
                 max_qubits,
                 code_distance;
    const DecodingMethod decoding_method;
private:
    /*
     * `front_layer_` contains `HistoryEvent` that correspond
     * to the oldest, undecoded event for each qubit.
     *
     * `back_layer_` is fixed-width and contains one entry
     * per qubit. The same `HistoryEvent` may be at two
     * indices.
     * */
    std::vector<HistoryEvent*> front_layer_,
                               back_layer_;
    size_t event_count_{0};

    /*
     * We have `History` auto-maintain an ancilla pool.
     * */
    std::vector<qubit_type> ancilla_pool_;
public:
    History(size_t max_program_qubits, size_t code_distance, DecodingMethod);
    ~History();

    HistoryEvent* front(qubit_type q) const { return front_layer_[q]; }
    HistoryEvent* back(qubit_type q) const { return back_layer_[q]; }

    /*
     * Retires the fully-decoded front event on `q`, cascading resolution
     * through the predecessor graph. Returns the conditional-basis (T-like)
     * ancillas freed during this call -- the only ancillas the Driver
     * lifetime-tracks -- so it can drop their `anc_available_cycle_` entries.
     * */
    std::vector<qubit_type> retire_front_event(qubit_type);

    void add_idle(qubit_type, cycle_type duration);
    event_add_result_type add_events_for_instructions(inst_ptr);
    
    size_t event_count() const { return event_count_; }
    bool can_decode_ooo() const { return decoding_method == DecodingMethod::PWD; }
private:
    qubit_type get_ancilla();

    void push_back_event(HistoryEvent*);
    void push_back_events(std::initializer_list<HistoryEvent*> arr) { for (auto* e : arr) push_back_event(e); }

    /*
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

    event_add_result_type add_cx_like_instruction(inst_ptr);
    event_add_result_type add_s_like_instruction(inst_ptr);
    event_add_result_type add_t_like_instruction(inst_ptr);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::ostream& operator<<(std::ostream&, const HistoryEvent&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace rs

#endif
