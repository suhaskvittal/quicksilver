/*
 *  author: Suhas Vittal
 *  date:   10 November 2026
 *  contributor: OpenAI Codex (RAD integration and recovery, 10 November 2026)
 *  contributor: OpenAI Codex (RAD detection cutoff, 11 November 2026)
 * */

#define TEMPL_PARAM template <class S1, class S2>
#define TEMPL_CLASS RADSystem<S1,S2>

#include <numeric>

namespace sim::ds
{

TEMPL_PARAM
TEMPL_CLASS::intern_l2_subsystem::intern_l2_subsystem(double freq_khz, size_t d, S2 s,
                                                       double error_rate, size_t client_count,
                                                       size_t max_depth, size_t l1_decoder_count,
                                                       S1 l1_sampler)
    // Size the independent L2 pool using the sampled parallel batch time.
    :PhenoDecoderSubsystem<S2>(freq_khz, d, s,
                               matched_decoder_count(l1_decoder_count, l1_sampler, s)),
    l1_per_cycle_error_rate(error_rate),
    l2_decode_volume(2*d*this->decoder_count),
    pending_by_depth_(client_count, std::vector<size_t>(max_depth+1, 0))
{
    last_event_end_cycle_.reserve(256);
    for (size_t i = 1; i < chunk_decode_end_cycle_.size(); i++)
        chunk_decode_end_cycle_[i] = chunk_decode_end_cycle_[i-1] + this->reaction_time();
}

TEMPL_PARAM void
TEMPL_CLASS::intern_l2_subsystem::enqueue_event(cycle_type start, cycle_type end, size_t stv,
                                                 std::vector<Qubit*> qubits, inst_ptr inst,
                                                 client_id_type client, size_t depth, bool accurate)
{
    for (auto* q : qubits)
    {
        const cycle_type last_end = last_event_end_cycle_[q];
        if (start > last_end)
            stv += start-last_end;
        last_event_end_cycle_[q] = end;
    }

    // in terms of error calculation, we only care about the most recent `l2_decode_volume` from
    // `stv` as any errors in the remaining volume would already be caught
    stv = std::min(stv, l2_decode_volume);

    const bool erroneous = !accurate && fpr_(rng_) < std::min(1.0, stv*l1_per_cycle_error_rate);
    while (end > chunk_decode_end_cycle_[1])
    {
        chunk_decode_end_cycle_[0] = chunk_decode_end_cycle_[1];
        chunk_decode_end_cycle_[1] = chunk_decode_end_cycle_[2];
        chunk_decode_end_cycle_[2] += this->reaction_time();
    }
    const cycle_type when = end <= chunk_decode_end_cycle_[0]
                                ? chunk_decode_end_cycle_[1]
                                : chunk_decode_end_cycle_[2];
    const bool is_t = is_t_like_instruction(inst->type);
    event_queue_.push(event_type{is_t ? inst : nullptr, inst->type,
                                 std::move(qubits), when, next_sequence_++, client, depth, erroneous});
    pending_by_depth_.at(static_cast<size_t>(client)).at(depth)++;
}

TEMPL_PARAM size_t
TEMPL_CLASS::intern_l2_subsystem::pending(client_id_type client, size_t depth) const
{
    return pending_by_depth_.at(static_cast<size_t>(client)).at(depth);
}

TEMPL_PARAM size_t
TEMPL_CLASS::intern_l2_subsystem::pending(client_id_type client) const
{
    const auto& counts = pending_by_depth_.at(static_cast<size_t>(client));
    return std::accumulate(counts.begin(), counts.end(), size_t{0});
}

TEMPL_PARAM std::optional<cycle_type>
TEMPL_CLASS::intern_l2_subsystem::next_event_cycle() const
{
    if (event_queue_.empty())
        return std::nullopt;
    return event_queue_.top().when;
}

TEMPL_PARAM long
TEMPL_CLASS::intern_l2_subsystem::process_due(cycle_type through)
{
    long progress{0};
    while (!event_queue_.empty() && event_queue_.top().when <= through)
    {
        event_type e = event_queue_.top();
        event_queue_.pop();

        // All events issued before this detection are already in the queue.
        // Events submitted afterward, including fixup, must not inherit it.
        uint64_t taint_cutoff = e.erroneous ? next_sequence_ : 0;
        for (auto* q : e.qubits)
        {
            auto it = taint_cutoff_by_qubit_.find(q);
            if (it != taint_cutoff_by_qubit_.end() && e.sequence < it->second)
                taint_cutoff = std::max(taint_cutoff, it->second);
        }

        if (is_t_like_instruction(e.type))
        {
            e.t_inst->rx.verified = true;
            e.t_inst->rx.erroneous = taint_cutoff != 0;
            for (auto* q : e.qubits)
                taint_cutoff_by_qubit_.erase(q);
        }
        else if (taint_cutoff != 0)
        {
            for (auto* q : e.qubits)
                taint_cutoff_by_qubit_[q] = taint_cutoff;
        }

        pending_by_depth_[static_cast<size_t>(e.client)][e.depth]--;
        progress++;
    }
    return progress;
}

TEMPL_PARAM long
TEMPL_CLASS::intern_l2_subsystem::operate()
{
    return process_due(this->current_cycle());
}

TEMPL_PARAM void
TEMPL_CLASS::intern_l2_subsystem::fast_forward_to_cycle(cycle_type target)
{
    Operable::fast_forward_to_cycle(target);
    if (target > 0)
        process_due(target-1);
}

TEMPL_PARAM
TEMPL_CLASS::RADSystem(double freq_khz, size_t d, S1 l1, S2 l2, double error_rate,
                       size_t client_count, size_t _rfifo_capacity, size_t _wpfifo_depth)
    :RADController(freq_khz),
    rfifo_capacity(_rfifo_capacity),
    wpfifo_depth(_wpfifo_depth),
    l1_(freq_khz, d, l1),
    l2_(freq_khz, d, l2, error_rate, client_count, wpfifo_depth,
        l1_.decoder_count, l1),
    clients_(client_count)
{}

TEMPL_PARAM void
TEMPL_CLASS::process_event(cycle_type start, cycle_type end, size_t stv,
                           std::vector<Qubit*> qubits, inst_ptr inst)
{
    const client_id_type client = qubits.front()->client_id;
    const size_t depth = inst->rx.is_non_program_instruction ? state(client).frames.size() : 0;
    const bool accurate = state(client).frames.size() >= wpfifo_depth;
    l2_.enqueue_event(start, end, stv, std::move(qubits), inst, client, depth, accurate);
}

TEMPL_PARAM cycle_type
TEMPL_CLASS::reaction_time_for(Qubit* q)
{
    if (state(q->client_id).frames.size() >= wpfifo_depth)
        return l2_.reaction_time();
    return l1_.reaction_time();
}

TEMPL_PARAM bool
TEMPL_CLASS::has_room(client_id_type client, size_t reserved) const
{
    return reserved < rfifo_capacity && state(client).rfifo.size() < rfifo_capacity-reserved;
}

TEMPL_PARAM bool
TEMPL_CLASS::can_issue_program(client_id_type client, const std::vector<Qubit*>& qubits) const
{
    const auto& frames = state(client).frames;
    if (frames.empty())
        return true;
    if (frames.back().resolving || frames.size() >= 2)
        return false;
    for (auto* q : qubits)
        for (const auto& frame : frames)
            if (frame.tainted.contains(q->qubit_id))
                return false;
    return true;
}

TEMPL_PARAM void
TEMPL_CLASS::retire_instruction(client_id_type client, std::unique_ptr<Instruction> inst)
{
    auto& rfifo = state(client).rfifo;
    if (rfifo.size() >= rfifo_capacity)
        std::cerr << "RAD retirement FIFO overflow" << _die{};
    rfifo.push_back(fifo_entry{std::move(inst), 0});
    s_max_rfifo_occupancy = std::max(s_max_rfifo_occupancy, rfifo.size());
}

TEMPL_PARAM Instruction*
TEMPL_CLASS::next_fixup(client_id_type client)
{
    auto& frames = state(client).frames;
    if (frames.empty() || frames.back().resolving || frames.back().pending.empty())
        return nullptr;
    return frames.back().pending.front().get();
}

TEMPL_PARAM void
TEMPL_CLASS::retire_fixup(client_id_type client)
{
    auto& st = state(client);
    auto& frame = st.frames.back();
    if (st.rfifo.size() >= rfifo_capacity)
        std::cerr << "RAD retirement FIFO overflow during fixup" << _die{};
    st.rfifo.push_back(fifo_entry{std::move(frame.pending.front()), st.frames.size()});
    frame.pending.pop_front();
    frame.outstanding++;
    s_fixup_uops++;
    s_max_rfifo_occupancy = std::max(s_max_rfifo_occupancy, st.rfifo.size());
}

TEMPL_PARAM bool
TEMPL_CLASS::idle(client_id_type client) const
{
    const auto& st = state(client);
    return st.rfifo.empty() && st.frames.empty() && l2_.pending(client) == 0;
}

TEMPL_PARAM bool
TEMPL_CLASS::touches(const Instruction* inst, const std::unordered_set<qubit_type>& tainted)
{
    for (auto q = inst->q_begin(); q != inst->q_end(); ++q)
        if (tainted.contains(*q))
            return true;
    return false;
}

TEMPL_PARAM void
TEMPL_CLASS::start_or_extend_recovery(client_state& st, fifo_entry& entry)
{
    if (st.frames.empty() || !st.frames.back().resolving)
    {
        if (st.frames.size() >= wpfifo_depth)
        {
            // An older L1 result can arrive after the final level starts. Resolve it
            // within the accurate-decoder level, ahead of its remaining fixup work.
            st.frames.back().resolving = true;
        }
        else
        {
            st.frames.emplace_back();
            s_max_recursion_depth = std::max(s_max_recursion_depth, st.frames.size());
        }
    }
    auto& frame = st.frames.back();
    frame.tainted.insert(entry.inst->q_begin(), entry.inst->q_end());
    frame.path.push_back(std::move(entry.inst));
}

TEMPL_PARAM Instruction::Type
TEMPL_CLASS::inverse_type(Instruction::Type type)
{
    using T = Instruction::Type;
    switch (type)
    {
    case T::S:    return T::SDG;
    case T::SDG:  return T::S;
    case T::SX:   return T::SXDG;
    case T::SXDG: return T::SX;
    case T::T:    return T::TDG;
    case T::TDG:  return T::T;
    case T::TX:   return T::TXDG;
    case T::TXDG: return T::TX;
    default:      return type;
    }
}

TEMPL_PARAM void
TEMPL_CLASS::build_fixup(recovery_frame& frame)
{
    std::deque<std::unique_ptr<Instruction>> new_pending;
    auto append = [&new_pending] (Instruction::Type type, const Instruction* source)
    {
        if (type == Instruction::Type::MZ || type == Instruction::Type::MX)
            return;
        auto inst = std::make_unique<Instruction>(type, source->q_begin(), source->q_end());
        inst->rx.is_non_program_instruction = true;
        new_pending.push_back(std::move(inst));
    };
    for (auto it = frame.path.rbegin(); it != frame.path.rend(); ++it)
        append(inverse_type((*it)->type), it->get());
    for (const auto& inst : frame.path)
        append(inst->type, inst.get());
    while (!frame.pending.empty())
    {
        new_pending.push_back(std::move(frame.pending.front()));
        frame.pending.pop_front();
    }
    frame.pending.swap(new_pending);
    frame.path.clear();
    frame.resolving = false;
}

TEMPL_PARAM long
TEMPL_CLASS::operate()
{
    long progress{0};
    for (size_t client = 0; client < clients_.size(); client++)
    {
        auto& st = clients_[client];
        while (!st.rfifo.empty())
        {
            auto& entry = st.rfifo.front();
            if (is_t_like_instruction(entry.inst->type) && !entry.inst->rx.verified)
                break;

            const bool erroneous = is_t_like_instruction(entry.inst->type) && entry.inst->rx.erroneous;
            if (erroneous)
            {
                start_or_extend_recovery(st, entry);
            }
            else if (!st.frames.empty() && st.frames.back().resolving
                     && touches(entry.inst.get(), st.frames.back().tainted))
            {
                auto& frame = st.frames.back();
                frame.tainted.insert(entry.inst->q_begin(), entry.inst->q_end());
                frame.path.push_back(std::move(entry.inst));
            }

            if (entry.depth > 0)
                st.frames.at(entry.depth-1).outstanding--;
            st.rfifo.pop_front();
            progress++;
        }

        if (!st.frames.empty() && st.frames.back().resolving
                && st.rfifo.empty() && l2_.pending(static_cast<client_id_type>(client)) == 0)
            build_fixup(st.frames.back());

        while (!st.frames.empty())
        {
            auto& frame = st.frames.back();
            if (frame.resolving || !frame.pending.empty() || frame.outstanding > 0
                    || l2_.pending(static_cast<client_id_type>(client), st.frames.size()) > 0)
                break;
            st.frames.pop_back();
            if (st.frames.empty())
                s_wrong_paths++;
            progress++;
        }
    }
    return progress;
}

} // namespace sim::ds

#undef TEMPL_PARAM
#undef TEMPL_CLASS
