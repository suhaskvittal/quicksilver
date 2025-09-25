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

MEMORY_COMPILER::MEMORY_COMPILER(size_t cmp_count, bool enable_rotation_directed_prefetch, uint64_t print_progress_freq)
    :cmp_count_(cmp_count),
    using_rotation_directed_prefetch_(enable_rotation_directed_prefetch),
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
            bool is_ready = std::all_of(inst->qubits.begin(), inst->qubits.end(),
                                        [this, inst] (qubit_type q) { return this->inst_windows_[q].front() == inst; });
            bool all_qubits_are_avail = std::all_of(inst->qubits.begin(), inst->qubits.end(),
                                                    [this] (qubit_type q) { return std::find(qubits_in_cmp_.begin(), qubits_in_cmp_.end(), q) != qubits_in_cmp_.end(); });
            if (is_ready && all_qubits_are_avail)
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
    // score each qubit based on how many times it is used in the pending buffer:
    // these determine two things:
    //    1. which qubits we should prioritize instructions for
    //    2. which qubits we cannot evict
    std::vector<double> qubit_scores(num_qubits_, 0.0);

    size_t num_inst_to_read = std::min(pending_inst_buffer_.size(), cmp_count_);
    for (size_t i = 0; i < num_inst_to_read; i++)
    {
        inst_ptr inst = pending_inst_buffer_[i];
        for (qubit_type q : inst->qubits)
            qubit_scores[q] += pow(2.0, -static_cast<double>(i));
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

            // make sure that this qubit's instruction at the head of the window fits in the working set:
            bool inst_at_head_fits = inst_windows_[i].front()->qubits.size() <= (cmp_count_ - new_working_set.size());
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
        for (qubit_type q : inst->qubits)
        {
            if (visited.count(q))
                continue;
            new_working_set.push_back(q);
            visited.insert(q);
        }
    }

    size_t remaining_bandwidth = cmp_count_ - new_working_set.size();

    // need to convert `qubits_in_cmp_` to `new_working_set`:
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
                remove_useless_memory_instructions_to_qubit(victim);

            outgoing_inst_buffer_.push_back(mswap);

            // update the qubit in the working set:
            qubits_in_cmp_[evict_idx] = q;
            qubit_use_count_[evict_idx] = 0;
        }
    }

    if (using_rotation_directed_prefetch_)
    {
        // check if any qubits in the working set have a rotation at the head of their window:
        for (size_t i = 0; i < new_working_set.size() && remaining_bandwidth > 0; i++)
        {
            qubit_type q = new_working_set[i];
            inst_ptr inst = inst_windows_[q].front();
            if (inst->type == INSTRUCTION::TYPE::RX || inst->type == INSTRUCTION::TYPE::RZ)
                remaining_bandwidth -= do_rotation_directed_prefetch(inst, remaining_bandwidth, qubit_scores);
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
MEMORY_COMPILER::remove_useless_memory_instructions_to_qubit(qubit_type q)
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

size_t
MEMORY_COMPILER::do_rotation_directed_prefetch(inst_ptr inst, size_t pf_bandwidth, const std::vector<double>& qubit_scores)
{
    qubit_type q = inst->qubits[0];
    const auto& win = inst_windows_[q];

    // accumulate prefetch targets from `win`
    size_t max_inst_to_prefetch = std::min(win.size(), size_t{8});
    std::vector<qubit_type> pf_targets;
    for (size_t i = 1; i < max_inst_to_prefetch; i++)
    {
        inst_ptr pf_inst = win[i];
        
        // try and prefetch operands of `pf_inst` that are not `q`
        for (qubit_type pf_target : pf_inst->qubits)
        {
            if (pf_target == q)
                continue;
            if (std::find(qubits_in_cmp_.begin(), qubits_in_cmp_.end(), pf_target) != qubits_in_cmp_.end())
                continue;

            pf_targets.push_back(pf_target);
        }
    }

    size_t prefetches = 0;
    std::sort(pf_targets.begin(), pf_targets.end(), 
            [&qubit_scores] (qubit_type a, qubit_type b) { return qubit_scores[a] > qubit_scores[b]; });

    // prefetch the targets in order of their scores:
    for (size_t i = 0; i < pf_targets.size() && prefetches < pf_bandwidth; i++)
    {
        qubit_type pf_target = pf_targets[i];
        // find victim:
        ssize_t evict_idx = compute_victim_index(pf_target, qubit_scores, {q});
        if (evict_idx < 0)
            throw std::runtime_error("no qubit to evict");

        // emit mprefetch instruction:
        qubit_type victim = qubits_in_cmp_[evict_idx];
        inst_ptr mprefetch = new INSTRUCTION(INSTRUCTION::TYPE::MPREFETCH, {pf_target, victim});
        if (qubit_use_count_[evict_idx] == 0)
            remove_useless_memory_instructions_to_qubit(victim);
        outgoing_inst_buffer_.push_back(mprefetch);

        // update the qubit in the working set:
        qubits_in_cmp_[evict_idx] = pf_target;
        qubit_use_count_[evict_idx] = 0;

        prefetches++;
    }

    return prefetches;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////