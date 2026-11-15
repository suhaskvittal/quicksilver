/*
 *  author: Suhas Vittal
 *  date:   10 November 2026
 *  contributor: OpenAI Codex (RAD integration and instruction ownership, 10 November 2026)
 *  contributor: OpenAI Codex (RAD detection cutoff, 11 November 2026)
 * */

#ifndef SIM_DS_RAD_h
#define SIM_DS_RAD_h

#include "sim/decoder_subsystem.h"
#include "sim/qubit.h"

#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <queue>
#include <random>
#include <unordered_map>
#include <unordered_set>

namespace sim::ds
{

class RADController : public DecoderSubsystemBase
{
public:
    using DecoderSubsystemBase::DecoderSubsystemBase;

    virtual bool has_room(client_id_type, size_t reserved=0) const =0;
    virtual bool can_issue_program(client_id_type, const std::vector<Qubit*>&) const =0;
    virtual void retire_instruction(client_id_type, std::unique_ptr<Instruction>) =0;
    virtual Instruction* next_fixup(client_id_type) =0;
    virtual void retire_fixup(client_id_type) =0;
    virtual bool idle(client_id_type) const =0;
    virtual std::optional<cycle_type> next_l2_event_cycle() const =0;
    virtual Operable* l2_operable() =0;
};

/* S1 and S2 supply independent latency samples for L1 and L2. */
template <class S1, class S2>
class RADSystem : public RADController
{
public:
    using intern_l1_subsystem = PhenoDecoderSubsystem<S1>;
    class intern_l2_subsystem : public PhenoDecoderSubsystem<S2>
    {
    public:
        struct event_type
        {
            inst_ptr t_inst;
            Instruction::Type type;
            std::vector<Qubit*> qubits;
            cycle_type when;
            uint64_t sequence;
            client_id_type client;
            size_t depth;
            bool erroneous;
        };
        struct event_cmp
        {
            bool operator()(const event_type& a, const event_type& b) const
            {
                return a.when == b.when ? a.sequence > b.sequence : a.when > b.when;
            }
        };

        const double l1_per_cycle_error_rate;
        /* l2 per-batch decode volume */
        const size_t l2_decode_volume;
    private:
        std::unordered_map<Qubit*, cycle_type> last_event_end_cycle_;
        // A detected error can only affect events issued before L2 detected it.
        // The value is the first event sequence that must not inherit the taint.
        std::unordered_map<Qubit*, uint64_t> taint_cutoff_by_qubit_;
        std::priority_queue<event_type, std::vector<event_type>, event_cmp> event_queue_;
        std::vector<std::vector<size_t>> pending_by_depth_;
        std::array<cycle_type, 3> chunk_decode_end_cycle_{};
        uint64_t next_sequence_{0};
        std::mt19937_64 rng_{0};
        std::uniform_real_distribution<double> fpr_{0.0, 1.0};
    public:
        intern_l2_subsystem(double freq_khz, size_t d, S2, double error_rate,
                            size_t client_count, size_t max_depth,
                            size_t l1_decoder_count, S1 l1_sampler);

        void enqueue_event(cycle_type start, cycle_type end, size_t stv,
                           std::vector<Qubit*> qubits, inst_ptr,
                           client_id_type client, size_t depth, bool accurate);
        size_t pending(client_id_type client) const;
        size_t pending(client_id_type client, size_t depth) const;
        std::optional<cycle_type> next_event_cycle() const;

        long operate() override;
    protected:
        void fast_forward_to_cycle(cycle_type target) override;
    private:
        long process_due(cycle_type through);
    };

    uint64_t s_wrong_paths{0};
    uint64_t s_fixup_uops{0};
    size_t s_max_rfifo_occupancy{0};
    size_t s_max_recursion_depth{0};
    const size_t rfifo_capacity;
    const size_t wpfifo_depth;
private:
    struct fifo_entry
    {
        std::unique_ptr<Instruction> inst;
        size_t depth;
    };
    struct recovery_frame
    {
        bool resolving{true};
        std::unordered_set<qubit_type> tainted;
        std::vector<std::unique_ptr<Instruction>> path;
        std::deque<std::unique_ptr<Instruction>> pending;
        size_t outstanding{0};

        recovery_frame() =default;
        recovery_frame(const recovery_frame&) =delete;
        recovery_frame(recovery_frame&&) noexcept =default;
    };
    struct client_state
    {
        std::deque<fifo_entry> rfifo;
        std::vector<recovery_frame> frames;
    };

    intern_l1_subsystem l1_;
    intern_l2_subsystem l2_;
    std::vector<client_state> clients_;
public:
    RADSystem(double freq_khz, size_t d, S1 l1, S2 l2, double error_rate,
              size_t client_count, size_t rfifo_capacity, size_t wpfifo_depth);

    void process_event(cycle_type start, cycle_type end, size_t stv,
                       std::vector<Qubit*> qubits, inst_ptr inst) override;
    cycle_type reaction_time() override { return l1_.reaction_time(); }
    cycle_type reaction_time_for(Qubit*) override;
    long operate() override;

    bool has_room(client_id_type, size_t reserved=0) const override;
    bool can_issue_program(client_id_type, const std::vector<Qubit*>&) const override;
    void retire_instruction(client_id_type, std::unique_ptr<Instruction>) override;
    Instruction* next_fixup(client_id_type) override;
    void retire_fixup(client_id_type) override;
    bool idle(client_id_type) const override;
    std::optional<cycle_type> next_l2_event_cycle() const override { return l2_.next_event_cycle(); }
    Operable* l2_operable() override { return &l2_; }

    intern_l1_subsystem& l1() { return l1_; }
    intern_l2_subsystem& l2() { return l2_; }
private:
    client_state& state(client_id_type client) { return clients_.at(static_cast<size_t>(client)); }
    const client_state& state(client_id_type client) const { return clients_.at(static_cast<size_t>(client)); }
    static Instruction::Type inverse_type(Instruction::Type);
    static bool touches(const Instruction*, const std::unordered_set<qubit_type>&);
    void start_or_extend_recovery(client_state&, fifo_entry&);
    void build_fixup(recovery_frame&);
};

} // namespace sim::ds

#include "rad.tpp"

#endif
