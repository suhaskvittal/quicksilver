/*
    author: Suhas Vittal
    date:   24 September 2025
*/

#include "memory/compiler.h"

#include <algorithm>
#include <numeric>
#include <unordered_set>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MEMORY_COMPILER::MEMORY_COMPILER(size_t cmp_count, EMIT_MEMORY_INST_IMPL emit_impl, uint64_t print_progress_freq)
    :cmp_count_(cmp_count),
    emit_impl_(emit_impl),
    print_progress_freq_(print_progress_freq),
    qubits_in_cmp_(cmp_count),
    qubit_use_count_(cmp_count, 0)
{
    std::iota(qubits_in_cmp_.begin(), qubits_in_cmp_.end(), 0);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
MEMORY_COMPILER::run(generic_strm_type& istrm, generic_strm_type& ostrm, uint64_t stop_after_completing_n_instructions)
{
    // reset stats:
    s_inst_read = 0;
    s_inst_done = 0;
    s_memory_instructions_added = 0;
    s_memory_prefetches_added = 0;
    s_unused_bandwidth = 0;

    // set number of qubits (first 4 bytes of input stream):
    generic_strm_read(istrm, &num_qubits_, sizeof(num_qubits_));
    generic_strm_write(ostrm, &num_qubits_, sizeof(num_qubits_));

    std::cout << "[ MEMORY_COMPILER ] num qubits: " << num_qubits_ << "\n";

    while (s_inst_done < stop_after_completing_n_instructions && (pending_inst_buffer_.size() > 0 || !generic_strm_eof(istrm)))
    {
        if (!generic_strm_eof(istrm))
            read_instructions(istrm);

        // check if there are any ready instructions:
        size_t num_inst_completed{0};
        for (size_t i = 0; i < pending_inst_buffer_.size(); i++)
        {
            inst_ptr inst = pending_inst_buffer_[i];
            bool is_software_inst = inst->type == INSTRUCTION::TYPE::X 
                                    || inst->type == INSTRUCTION::TYPE::Y
                                    || inst->type == INSTRUCTION::TYPE::Z
                                    || inst->type == INSTRUCTION::TYPE::SWAP;
            bool is_ready = std::all_of(inst->qubits.begin(), inst->qubits.end(),
                                        [this, inst] (qubit_type q) { return this->inst_windows_[q].front() == inst; });
            bool all_qubits_are_avail = std::all_of(inst->qubits.begin(), inst->qubits.end(),
                                                    [this] (qubit_type q) { return std::find(qubits_in_cmp_.begin(), qubits_in_cmp_.end(), q) != qubits_in_cmp_.end(); });
            if (is_ready && (all_qubits_are_avail || is_software_inst))
            {
                // update qubit usages:
                for (qubit_type q : inst->qubits)
                {
                    auto it = std::find(qubits_in_cmp_.begin(), qubits_in_cmp_.end(), q);
                    size_t idx = std::distance(qubits_in_cmp_.begin(), it);
                    qubit_use_count_[idx]++;
                }

                // move the instruction to the outgoing buffer:
                outgoing_inst_buffer_.push_back(inst);
                
                // clear out the buffer location so we can delete it later via `std::remove`
                pending_inst_buffer_[i] = nullptr;

                // delete the instructions from its qubits' windows
                for (qubit_type q : inst->qubits)
                {
                    if (inst_windows_[q].front() != inst)
                        throw std::runtime_error("instruction at head of window is not the same as the one being completed");
                    inst_windows_[q].pop_front();
                }

                if (!is_software_inst)
                {
                    std::cout << num_inst_completed << " : "<< *inst << "\n";
                }

                num_inst_completed++;
            }
        }

        if (num_inst_completed > 0)
        {
            auto it = std::remove(pending_inst_buffer_.begin(), pending_inst_buffer_.end(), nullptr);
            pending_inst_buffer_.erase(it, pending_inst_buffer_.end());

            uint64_t prev_inst_done = s_inst_done;
            s_inst_done += num_inst_completed;

            if (print_progress_freq_ && (print_progress_freq_ == 1 || (s_inst_done % print_progress_freq_) < (prev_inst_done % print_progress_freq_)))
            {
                size_t num_mem = std::count_if(outgoing_inst_buffer_.begin(), outgoing_inst_buffer_.end(),
                                                [](inst_ptr inst) { return inst->type == INSTRUCTION::TYPE::MSWAP || inst->type == INSTRUCTION::TYPE::MPREFETCH; });
                size_t num_mprefetch = std::count_if(outgoing_inst_buffer_.begin(), outgoing_inst_buffer_.end(),
                                                    [](inst_ptr inst) { return inst->type == INSTRUCTION::TYPE::MPREFETCH; });

                num_mem += s_memory_instructions_added;
                num_mprefetch += s_memory_prefetches_added;

                std::cout << "[ MEMORY_COMPILER ] progress: " << s_inst_done << " instructions processed, " << num_mem << " memory instructions, " << num_mprefetch << " prefetches" << std::endl;
            }

            // handle the outgoing buffer if it is too large:
            if (outgoing_inst_buffer_.size() > OUTGOING_INST_BUFFER_SIZE)
            {
                // commit the instructions to the output stream:
                auto begin = outgoing_inst_buffer_.begin();
                auto end = outgoing_inst_buffer_.begin() + OUTGOING_INST_BUFFER_SIZE/2;  // only read out half the buffer
            
                drain_outgoing_buffer(ostrm, begin, end);
                // remove the committed instructions from the outgoing buffer:
                outgoing_inst_buffer_.erase(begin, end);
            }
        }
        else
        {
            // there are no ready instructions, so we need to emit memory instructions to make progress
            emit_memory_instructions();
        }

        s_timestep++;
    }

    // drain the rest of the outgoing buffer:
    drain_outgoing_buffer(ostrm, outgoing_inst_buffer_.begin(), outgoing_inst_buffer_.end());
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
MEMORY_COMPILER::read_instructions(generic_strm_type& istrm)
{
    if (pending_inst_buffer_.size() >= PENDING_INST_BUFFER_SIZE)
        return;

    for (size_t i = 0; i < READ_LIMIT && !generic_strm_eof(istrm); i++)
    {
        INSTRUCTION::io_encoding enc;
        enc.read_write([&istrm] (void* buf, size_t size) { return generic_strm_read(istrm, buf, size); });
        inst_ptr inst = new INSTRUCTION(std::move(enc));
        inst->inst_number = s_inst_read++;

        // add the instruction to the pending buffer:
        pending_inst_buffer_.push_back(inst);
        // and the instruction window for each qubit:
        for (qubit_type q : inst->qubits)
            inst_windows_[q].push_back(inst);
    }
}

void
MEMORY_COMPILER::drain_outgoing_buffer(generic_strm_type& ostrm, std::vector<inst_ptr>::iterator begin, std::vector<inst_ptr>::iterator end)
{
    std::for_each(begin, end, 
                [this, &ostrm] (inst_ptr inst)
                {
                    auto enc = inst->serialize();
                    enc.read_write([&ostrm] (void* buf, size_t size) { return generic_strm_write(ostrm, buf, size); });

                    this->s_memory_instructions_added += (inst->type == INSTRUCTION::TYPE::MSWAP || inst->type == INSTRUCTION::TYPE::MPREFETCH);
                    this->s_memory_prefetches_added += (inst->type == INSTRUCTION::TYPE::MPREFETCH);

                    delete inst;
                });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
MEMORY_COMPILER::emit_memory_instructions()
{
    size_t remaining_bandwidth;
    switch (emit_impl_)
    {
    case EMIT_MEMORY_INST_IMPL::VISZLAI:
        remaining_bandwidth = emit_viszlai();
        break;
    
    case EMIT_MEMORY_INST_IMPL::SCORE_BASED:
        remaining_bandwidth = emit_score_based();
        break;

    default:
        throw std::runtime_error("invalid emit implementation");
    }

    s_unused_bandwidth += remaining_bandwidth;
    s_emission_calls++;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
MEMORY_COMPILER::emit_viszlai()
{
    // iterate and do:
    //    1. get all ready instructions at a given time step (layer)
    //    2. complete them greedily and move onto the next layer

    std::vector<qubit_type> new_working_set;
    std::unordered_set<inst_ptr> visited;

    auto inst_sel_iter = [&new_working_set, &visited, this] (inst_ptr inst)
    {
        if (visited.count(inst))
            return;
        visited.insert(inst);

        if (inst->qubits.size() > this->cmp_count_ - new_working_set.size())
            return;

        bool ready = inst->qubits.size() == 1 
                     || std::all_of(inst->qubits.begin(), inst->qubits.end(),
                                [this, inst] (qubit_type q) { return this->inst_windows_[q].front() == inst; });
        if (ready)
            new_working_set.insert(new_working_set.end(), inst->qubits.begin(), inst->qubits.end());
    };

    // first pass: check if any qubits in `qubits_in_cmp_` have a ready instruction at the head of the window:
    for (qubit_type q : qubits_in_cmp_)
    {
        const auto& win = inst_windows_[q];
        if (win.empty())
            continue;
        inst_sel_iter(win.front());
    }

    // second pass: check if any qubits in `inst_windows_` have a ready instruction at the head of the window:
    for (const auto& [q, win] : inst_windows_)
    {
        if (win.empty())
            continue;
        inst_sel_iter(win.front());
    }

    std::vector<double> qubit_scores(num_qubits_, 0.0);
    size_t remaining_bandwidth = cmp_count_ - new_working_set.size();
    transform_working_set_into(new_working_set, qubit_scores);

    return remaining_bandwidth;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
MEMORY_COMPILER::emit_score_based()
{
    // score each qubit based on how many times it is used in the pending buffer:
    // these determine two things:
    //    1. which qubits we should prioritize instructions for
    //    2. which qubits we cannot evict
    std::vector<double> qubit_scores(num_qubits_, 0.0);

    std::vector<size_t> io_cost(num_qubits_, 1);
    std::vector<size_t> qubit_depths(num_qubits_, 0);
    // initialize cost of qubits in the working set to 0
    for (qubit_type q : qubits_in_cmp_)
        io_cost[q] = 0;

    std::vector<double> inst_scores;

    size_t num_inst_to_read = std::min(pending_inst_buffer_.size(), size_t{2048});
    for (size_t i = 0; i < num_inst_to_read; i++)
    {
        inst_ptr inst = pending_inst_buffer_[i];
        
        if (inst->type == INSTRUCTION::TYPE::X 
            || inst->type == INSTRUCTION::TYPE::Y 
            || inst->type == INSTRUCTION::TYPE::Z 
            || inst->type == INSTRUCTION::TYPE::SWAP)
        {
            continue;
        }

        // count number of qubits that are not in the working set:
        size_t c{0};
        for (qubit_type q : inst->qubits)
            c += io_cost[q];

        double inst_score = 1.0 / static_cast<double>(c+1);

        for (qubit_type q : inst->qubits)
        {
            if (q == 2)
                std::cout << "inst: " << inst->to_string() << " score: " << inst_score << "\n";
            qubit_scores[q] += inst_score;
            io_cost[q] = c;
        }

        inst_scores.push_back(inst_score);
    }

    if (s_emission_calls % 1 == 0)
    {
        std::cout << "scores:\n";
        for (size_t i = 0; i < num_qubits_; i++)
        {
            if (qubit_scores[i] > 1.0)
                std::cout << "\t" << i << " : "<< qubit_scores[i] << "\n";
        }
        std::cout << "\n";
    }

    // create new working set based on scores:
    std::vector<qubit_type> new_working_set{};
    new_working_set.reserve(cmp_count_);

    std::unordered_set<qubit_type> visited;
    while (new_working_set.size() < cmp_count_)
    {
        qubit_type q_best{-1};
        for (qubit_type i = 0; i < static_cast<qubit_type>(num_qubits_); i++)
        {
            if (visited.count(i))
                continue;
            if (inst_windows_[i].empty())
                continue;

            inst_ptr inst = inst_windows_[i].front();

            size_t num_need_to_add = std::count_if(inst->qubits.begin(), inst->qubits.end(),
                                                    [&new_working_set] (qubit_type q)
                                                    {
                                                        return std::find(new_working_set.begin(), new_working_set.end(), q) == new_working_set.end();
                                                    });
            // make sure that this qubit's instruction at the head of the window fits in the working set:
            bool inst_at_head_fits = num_need_to_add <= (cmp_count_ - new_working_set.size());
            if (!inst_at_head_fits)
                continue;

            if (q_best < 0 || qubit_scores[i] > qubit_scores[q_best])
                q_best = i;
        }

        if (q_best < 0)
            break;

        // get the ready instruction for this qubit:
        inst_ptr inst = inst_windows_[q_best].front();
        // add all qubits of this instruction to the working set:
        std::cout << q_best << " is chosen, installing qubits  on behalf of: " << inst->to_string() << "\n";
        for (qubit_type q : inst->qubits)
        {
            if (visited.count(q))
                continue;
            new_working_set.push_back(q);
            visited.insert(q);
        }
    }

    if (s_emission_calls % 1 == 0)
    {
        std::cout << "old working set:\t";
        for (qubit_type q : qubits_in_cmp_)
            std::cout << " " << q;
        std::cout << "\n";
    }

    transform_working_set_into(new_working_set, qubit_scores);
    size_t remaining_bandwidth = cmp_count_ - new_working_set.size();

    if (s_emission_calls % 1 == 0)
    {
        std::cout << "new working set:\t";
        for (qubit_type q : qubits_in_cmp_)
            std::cout << " " << q;
        std::cout << "\n";
    }

    return remaining_bandwidth;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
MEMORY_COMPILER::transform_working_set_into(const std::vector<qubit_type>& new_working_set, const std::vector<double>& qubit_scores)
{
    for (qubit_type q : new_working_set)
    {
        auto it = std::find(qubits_in_cmp_.begin(), qubits_in_cmp_.end(), q);
        if (it == qubits_in_cmp_.end())
        {
            // need to find a qubit with the lowest score in `qubits_in_cmp_` and remove it
            ssize_t evict_idx = compute_victim_index(q, qubit_scores, new_working_set);
            if (evict_idx < 0)
                throw std::runtime_error("no qubit to evict");

            // emit mswap instruction:
            qubit_type victim = qubits_in_cmp_[evict_idx];
            inst_ptr mswap = new INSTRUCTION(INSTRUCTION::TYPE::MSWAP, {q, victim});

            // first, check if the `victim` has even been used once:
            // if not, there is a useless mswap/mprefetch instruction mapping to this qubit:
            if (qubit_use_count_[evict_idx] == 0)
                remove_last_memory_instruction_to_qubit(victim);

            outgoing_inst_buffer_.push_back(mswap);

            // update the qubit in the working set:
            qubits_in_cmp_[evict_idx] = q;
            qubit_use_count_[evict_idx] = 0;

            // update stats for lifetime:
            qubit_timestep_entered_working_set_[q] = s_timestep; 
            s_total_lifetime_in_working_set += s_timestep - qubit_timestep_entered_working_set_[victim];
            s_num_lifetimes_recorded++;
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ssize_t
MEMORY_COMPILER::compute_victim_index(qubit_type q, const std::vector<double>& qubit_scores, const std::vector<qubit_type>& do_not_evict)
{
    ssize_t evict_idx{-1};
    for (ssize_t i = 0; i < qubits_in_cmp_.size(); i++)
    {
        // if this qubit is in the working set, skip it
        qubit_type e = qubits_in_cmp_[i];
        if (std::find(do_not_evict.begin(), do_not_evict.end(), e) != do_not_evict.end())
            continue;
        if (evict_idx < 0 || qubit_scores[e] < qubit_scores[qubits_in_cmp_[evict_idx]])
            evict_idx = i;
    }

    return evict_idx;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
MEMORY_COMPILER::remove_last_memory_instruction_to_qubit(qubit_type q)
{
    auto it = std::find_if(pending_inst_buffer_.rbegin(), pending_inst_buffer_.rend(),
                            [q] (inst_ptr inst) 
                            { 
                                return (inst->type == INSTRUCTION::TYPE::MSWAP || inst->type == INSTRUCTION::TYPE::MPREFETCH)
                                        && inst->qubits[0] == q;
                            });
    if (it != pending_inst_buffer_.rend())
    {
        delete (*it);
        pending_inst_buffer_.erase(it.base() - 1);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////