/*
    author: Suhas Vittal
    date:   24 September 2025
*/

#include "compiler/memopt.h"
#include "compiler/memopt/impl/viszlai.h"
#include "compiler/memopt/impl/cost_aware.h"

#include <algorithm>
#include <numeric>
#include <unordered_set>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MEMOPT::MEMOPT(size_t cmp_count, EMIT_IMPL_ID emit_impl_id, uint64_t print_progress_freq)
    :cmp_count_(cmp_count),
    print_progress_freq_(print_progress_freq),
    working_set_()
{
    // initialize the working set:
    working_set_.reserve(cmp_count);
    for (qubit_type q = 0; q < cmp_count; q++)
        working_set_.insert(q);

    // initialize the emit implementation:
    if (emit_impl_id == EMIT_IMPL_ID::VISZLAI)
        emit_impl_ = std::make_unique<memopt::impl::VISZLAI>(cmp_count);
    else if (emit_impl_id == EMIT_IMPL_ID::COST_AWARE)
        emit_impl_ = std::make_unique<memopt::impl::COST_AWARE>(cmp_count);
    else
        throw std::runtime_error("invalid emit implementation");
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
MEMOPT::run(generic_strm_type& istrm, generic_strm_type& ostrm, uint64_t stop_after_completing_n_instructions)
{
    // reset stats:
    s_inst_read = 0;
    s_inst_done = 0;
    s_unrolled_inst_done = 0;
    s_memory_instructions_added = 0;
    s_memory_prefetches_added = 0;
    s_unused_bandwidth = 0;

    // set number of qubits (first 4 bytes of input stream):
    generic_strm_read(istrm, &num_qubits_, sizeof(num_qubits_));
    generic_strm_write(ostrm, &num_qubits_, sizeof(num_qubits_));

    emit_impl_->num_qubits = num_qubits_;

    std::cout << "[ MEMOPT ] num qubits: " << num_qubits_ << "\n";

    while (s_unrolled_inst_done < stop_after_completing_n_instructions 
            && (pending_inst_buffer_.size() > 0 || !generic_strm_eof(istrm)))
    {
        if (!generic_strm_eof(istrm))
            read_instructions(istrm);

        // check if there are any ready instructions:
        size_t num_unrolled_inst_completed{0};
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
                                                    [this] (qubit_type q) { return this->working_set_.count(q); });
            if (is_ready && (all_qubits_are_avail || is_software_inst))
            {
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
                // handle unrolled inst:
                if (inst->type == INSTRUCTION::TYPE::RZ || inst->type == INSTRUCTION::TYPE::RX)
                    num_unrolled_inst_completed += inst->urotseq.size();
                else if (inst->type == INSTRUCTION::TYPE::CCX)
                    num_unrolled_inst_completed += 15;  // 15 uops for CCX, 13 for CCZ
                else if (inst->type == INSTRUCTION::TYPE::CCZ)
                    num_unrolled_inst_completed += 13;
                else
                    num_unrolled_inst_completed++;
            }
        }

        if (num_inst_completed > 0)
        {
            uint64_t prev_inst_done = s_unrolled_inst_done;

            s_inst_done += num_inst_completed;
            s_unrolled_inst_done += num_unrolled_inst_completed;

            auto it = std::remove(pending_inst_buffer_.begin(), pending_inst_buffer_.end(), nullptr);
            pending_inst_buffer_.erase(it, pending_inst_buffer_.end());

            bool pp = (print_progress_freq_ == 1)
                    || (s_unrolled_inst_done % print_progress_freq_) < (prev_inst_done % print_progress_freq_);
            if (print_progress_freq_ && pp)
            {
                size_t num_mem{0}, num_mprefetch{0};
                for (auto* inst : outgoing_inst_buffer_)
                {
                    num_mem += (inst->type == INSTRUCTION::TYPE::MSWAP || inst->type == INSTRUCTION::TYPE::MPREFETCH);
                    num_mprefetch += (inst->type == INSTRUCTION::TYPE::MPREFETCH);
                }

                num_mem += s_memory_instructions_added;
                num_mprefetch += s_memory_prefetches_added;

                std::cout << "[ MEMOPT ] progress: " << s_inst_done << " instructions processed, " 
                            << s_unrolled_inst_done << " unrolled instructions done, "
                            << num_mem << " memory instructions, " 
                            << num_mprefetch << " prefetches\n";
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
MEMOPT::read_instructions(generic_strm_type& istrm)
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
MEMOPT::drain_outgoing_buffer(generic_strm_type& ostrm, std::vector<inst_ptr>::iterator begin, std::vector<inst_ptr>::iterator end)
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
MEMOPT::emit_memory_instructions()
{
    auto result = emit_impl_->emit_memory_instructions(working_set_, pending_inst_buffer_, inst_windows_);

    working_set_ = std::move(result.working_set);

    if (working_set_.size() != cmp_count_)
        throw std::runtime_error("working set size does not match number of qubits");

    outgoing_inst_buffer_.insert(outgoing_inst_buffer_.end(), result.memory_instructions.begin(), result.memory_instructions.end());
    s_unused_bandwidth += result.unused_bandwidth;
    s_emission_calls++;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool 
validate_schedule(generic_strm_type& ground_truth, generic_strm_type& test, size_t cmp_count)
{
    // skip the first four bytes from each strm:
    uint32_t unused_4b;
    generic_strm_read(ground_truth, &unused_4b, sizeof(unused_4b));
    generic_strm_read(test, &unused_4b, sizeof(unused_4b));

    inst_schedule_type gt_win, test_win;

    read_instructions(ground_truth, gt_win, cmp_count, false);
    if (!read_instructions(test, test_win, cmp_count, true))
        return false;

    if (!compare_instruction_windows(gt_win, test_win))
        return false;

    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
read_instructions(generic_strm_type& istrm, inst_schedule_type& inst_windows, size_t cmp_count, bool check_memory_access_validity)
{
    std::vector<qubit_type> qubits(cmp_count);
    std::iota(qubits.begin(), qubits.end(), 0);

    // check validity every `EPOCH_SIZE` instructions:
    while (!generic_strm_eof(istrm))
    {
        INSTRUCTION::io_encoding enc;
        enc.read_write([&istrm] (void* buf, size_t size) { return generic_strm_read(istrm, buf, size); });
        std::shared_ptr<INSTRUCTION> inst{new INSTRUCTION(std::move(enc))};

        if (inst->type == INSTRUCTION::TYPE::MSWAP || inst->type == INSTRUCTION::TYPE::MPREFETCH)
        {
            // make sure that the qubits are in the correct places:
            if (check_memory_access_validity)
            {
                auto q0 = inst->qubits[0],
                     q1 = inst->qubits[1];

                auto q0_it = std::find(qubits.begin(), qubits.end(), q0);
                auto q1_it = std::find(qubits.begin(), qubits.end(), q1);

                if (q0_it != qubits.end())
                {
                    std::cout << "[ VALIDATE ] qubit " << q0 
                            << " found in compute region: inst " << *inst << "\n";
                    return false;
                }

                if (q1_it == qubits.end())
                {
                    std::cout << "[ VALIDATE ] qubit " << q1 
                            << " not found in compute region: inst " << *inst << "\n";
                    return false;
                }

                *q1_it = q0;
            }
            continue;
        }

        bool is_software_inst = inst->type == INSTRUCTION::TYPE::X 
                                || inst->type == INSTRUCTION::TYPE::Y
                                || inst->type == INSTRUCTION::TYPE::Z
                                || inst->type == INSTRUCTION::TYPE::SWAP;

        if (check_memory_access_validity && !is_software_inst)
        {
            // check that the arguments are all in the compute region:
            bool all_present = std::all_of(inst->qubits.begin(), inst->qubits.end(),
                                            [&qubits] (qubit_type q) 
                                            {
                                                return std::find(qubits.begin(), qubits.end(), q) != qubits.end();
                                            });
            if (!all_present)
            {
                std::cout << "[ VALIDATE ] not all qubits found in compute region: inst " << *inst << "\n";
                return false;
            }
        }

        for (qubit_type q : inst->qubits)
            inst_windows[q].push_back(inst);
    }

    return true;
}

bool
compare_instruction_windows(const inst_schedule_type& gt, const inst_schedule_type& test)
{
    for (const auto& [q, gt_win] : gt)
    {
        auto test_it = test.find(q);
        if (test_it == test.end())
        {
            std::cout << "[ VALIDATE ] qubit " << q << " not found in test window\n";
            return false;
        }

        if (gt_win.size() != test_it->second.size())
        {
            std::cout << "[ VALIDATE ] qubit " << q << " window size mismatch: " 
                        << gt_win.size() << " != " << test_it->second.size() << "\n";
            return false;
        }

        const auto& test_win = test_it->second;
        for (size_t i = 0; i < gt_win.size(); i++)
        {
            bool type_matches = (gt_win[i]->type == test_win[i]->type);
            bool qubits_match = (gt_win[i]->qubits == test_win[i]->qubits);
            bool urotseq_matches = (gt_win[i]->urotseq == test_win[i]->urotseq);

            if (!type_matches || !qubits_match || !urotseq_matches)
            {
                std::cout << "[ VALIDATE ] qubit " << q << " instruction " << i << " mismatch:\n";
                std::cout << "\tground truth: " << *gt_win[i] << "\n";
                std::cout << "\ttest: " << *test_win[i] << "\n";
                return false;
            }
        }
    }

    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
