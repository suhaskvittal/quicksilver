/*
    author: Suhas Vittal
    date:   06 October 2025
*/

#include "compiler/program/rotation_manager.h"
#include "nwqec/gridsynth/gridsynth.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <sstream>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace prog
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

//#define ANGLE_USE_CFLOAT

#if defined(ANGLE_USE_CFLOAT)
using ANGLE_TYPE = COMPARABLE_FLOAT;
#else
using ANGLE_TYPE = INSTRUCTION::fpa_type;
#endif

ANGLE_TYPE
make_angle(const INSTRUCTION::fpa_type& rotation, ssize_t precision)
{
#if defined(ANGLE_USE_CFLOAT)
    return COMPARABLE_FLOAT{convert_fpa_to_float(rotation), precision};
#else
    return INSTRUCTION::fpa_type(rotation);
#endif
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct PROMISE
{
    using ptr = std::unique_ptr<PROMISE>;

    bool                            ready{false};
    size_t                          ref_count{1};
    std::vector<INSTRUCTION::TYPE>  urotseq;
};

struct PENDING_ENTRY
{
    using ptr = std::unique_ptr<PENDING_ENTRY>;

    INSTRUCTION::fpa_type rotation;
    ssize_t precision;
};

static std::deque<PENDING_ENTRY::ptr>                           RM_PENDING;
static std::unordered_map<ANGLE_TYPE, PROMISE::ptr>             RM_READY_MAP;
static std::mutex                                               RM_SCHED_LOCK;
static std::condition_variable                                  RM_PENDING_UPDATED;
static std::condition_variable                                  RM_VALUE_READY;

static std::unordered_map<std::thread::id, size_t> THREAD_ID_TO_INDEX;

static std::atomic<bool> RM_SIG_DONE{false};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
rotation_manager_init(size_t num_threads)
{
    // spawn `num_threads` and detach them:
    for (size_t i = 0; i < num_threads; i++)
    {
        std::thread th{ [] ()
        {
            while (!RM_SIG_DONE.load())
                rm_thread_iteration();
        }};
        THREAD_ID_TO_INDEX[th.get_id()] = i;
        th.detach();
    }
}

void
rotation_manager_end()
{
    std::unique_lock<std::mutex> sched_lock(RM_SCHED_LOCK);
    RM_SIG_DONE.store(true);
    std::cout << "main thread set RM_SIG_DONE to true\n";
    // wake up all threads waiting on `RM_PENDING_UPDATED`:
    RM_PENDING_UPDATED.notify_all();
    sched_lock.unlock();

    RM_READY_MAP.clear();
    RM_PENDING.clear();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
rm_schedule_synthesis(const INSTRUCTION::fpa_type& rotation, ssize_t precision)
{
    std::lock_guard<std::mutex> sched_lock(RM_SCHED_LOCK);
    PENDING_ENTRY::ptr p = std::make_unique<PENDING_ENTRY>(rotation, precision);
    RM_PENDING.push_back(std::move(p));
    RM_PENDING_UPDATED.notify_one();
}

std::vector<INSTRUCTION::TYPE>
rm_find(const INSTRUCTION::fpa_type& rotation, ssize_t precision)
{
    auto k = make_angle(rotation, precision);

    std::unique_lock<std::mutex> sched_lock(RM_SCHED_LOCK);
    
rm_get_entry:
    auto it = RM_READY_MAP.find(k);
    if (it == RM_READY_MAP.end() || !it->second->ready)
    {
//      std::cout << "thread " << THREAD_ID_TO_INDEX[std::this_thread::get_id()] << " waiting for rotation " << fpa::to_string(rotation) << ", precision = " << precision << "\n";
        RM_VALUE_READY.wait(sched_lock);
        goto rm_get_entry;
    }

    PROMISE::ptr& p = it->second;
    auto urotseq = p->urotseq;

    p->ref_count--;
    if (p->ref_count == 0)
        RM_READY_MAP.erase(it);
    sched_lock.unlock();

    return urotseq;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
rm_thread_iteration()
{
    // critical section: get pending rotation request:
    std::unique_lock<std::mutex> sched_lock(RM_SCHED_LOCK);
    while (RM_PENDING.empty() && !RM_SIG_DONE.load())
        RM_PENDING_UPDATED.wait(sched_lock);

    if (RM_SIG_DONE.load())
    {
        std::cout << "thread " << THREAD_ID_TO_INDEX[std::this_thread::get_id()] << " done\n";
        sched_lock.unlock();
        return;
    }

    PENDING_ENTRY::ptr entry = std::move(RM_PENDING.front());
    RM_PENDING.pop_front();
    auto k = make_angle(entry->rotation, entry->precision);
    size_t ref_count = 1;

    // insert into `RM_READY_MAP`:
    auto it = RM_READY_MAP.find(k);
    if (it == RM_READY_MAP.end())
    {
        PROMISE::ptr p = std::make_unique<PROMISE>();
        p->ref_count = ref_count;
        PROMISE* p_raw = p.get();  // need this after we move `p`
        RM_READY_MAP.insert({k, std::move(p)});
        sched_lock.unlock();

        // parallel region:
        p_raw->urotseq = rm_synthesize_rotation(entry->rotation, entry->precision);

        sched_lock.lock();

//      std::cout << "thread " << THREAD_ID_TO_INDEX[std::this_thread::get_id()] << " synthesized rotation " << fpa::to_string(entry->rotation) << ", precision = " << entry->precision << "\n";

        p_raw->ready = true;
        RM_VALUE_READY.notify_all();
    }
    else
    {
        it->second->ref_count++;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
rm_flip_h_subsequences(std::vector<INSTRUCTION::TYPE>& urotseq)
{
    size_t h_count = std::count(urotseq.begin(), urotseq.end(), INSTRUCTION::TYPE::H);

    auto begin = urotseq.begin();
    // while there are at least two H gates, flip the subsequence between them:
    while (h_count > 2)
    {
        auto h_begin = std::find(begin, urotseq.end(), INSTRUCTION::TYPE::H);
        if (h_begin == urotseq.end())
            break;

        auto h_end = std::find(h_begin+1, urotseq.end(), INSTRUCTION::TYPE::H);
        if (h_end == urotseq.end())
            break;

        std::for_each(h_begin+1, h_end, [](auto& g) { g = flip_basis(g); });

        // set the H gates to nil -- we will remove all NIL gates at the end:
        *h_begin = INSTRUCTION::TYPE::NIL;
        *h_end = INSTRUCTION::TYPE::NIL;

        begin = h_end+1;
        h_count -= 2;
    }

    auto it = std::remove_if(urotseq.begin(), urotseq.end(), [] (const auto& inst) { return inst == INSTRUCTION::TYPE::NIL; });
    urotseq.erase(it, urotseq.end());
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
rm_consolidate_and_reduce_subsequences(std::vector<INSTRUCTION::TYPE>& urotseq)
{
    // generate a subsequence, stop until we hit an H gate or gate in a different basis:
    BASIS_TYPE current_basis{BASIS_TYPE::NONE};
    int8_t current_rotation_sum{0};
    auto seq_begin = urotseq.begin();
    for (auto it = urotseq.begin(); it != urotseq.end(); it++)
    {
        auto g = *it;
        if (current_basis != BASIS_TYPE::NONE)
        {
            if (get_basis_type(g) != current_basis)
            {
                // replace the sequence with the appropriate gate:
                auto seq_kill_begin = rm_consolidate_gate(current_basis, current_rotation_sum, seq_begin);
                // set all gates from `seq_kill_begin` to `it` to `NIL` -- we will remove these later:
                std::fill(seq_kill_begin, it, INSTRUCTION::TYPE::NIL);
                
                // set basis type to none since we can now start a new subsequence
                current_basis = BASIS_TYPE::NONE;
                current_rotation_sum = 0;
            }
            else
            {
                current_rotation_sum += get_rotation_value(g);
                current_rotation_sum &= 7;  // mod 8
            }
        }

        // this is not an else since we may set `current_basis` to `BASIS_TYPE::NONE` in the above if statement
        if (current_basis == BASIS_TYPE::NONE)
        {
            if (g == INSTRUCTION::TYPE::H)
                continue;  // nothing to be done

            current_basis = get_basis_type(g);
            if (current_basis == BASIS_TYPE::NONE)
                throw std::runtime_error("invalid gate: " + std::string(BASIS_GATES[static_cast<size_t>(g)]));
            current_rotation_sum = get_rotation_value(g);
            seq_begin = it;
        }
    }

    // if we are still in a subsequence, finish it off:
    if (current_basis != BASIS_TYPE::NONE)
    {
        auto seq_kill_begin = rm_consolidate_gate(current_basis, current_rotation_sum, seq_begin);
        std::fill(seq_kill_begin, urotseq.end(), INSTRUCTION::TYPE::NIL);
    }

    // remove all `NIL` gates:
    auto it = std::remove_if(urotseq.begin(), urotseq.end(), [] (const auto& inst) { return inst == INSTRUCTION::TYPE::NIL; });
    urotseq.erase(it, urotseq.end());
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

constexpr size_t GS_CALL_PRINT_FREQUENCY = 100'000;
static std::atomic<size_t> GS_CALL_COUNT{0};

std::vector<INSTRUCTION::TYPE>
rm_synthesize_rotation(const INSTRUCTION::fpa_type& rotation, ssize_t precision)
{
    size_t gs_call_id = GS_CALL_COUNT.fetch_add(1);

    // call gridsynth:
    std::string fpa_str = fpa::to_string(rotation, fpa::STRING_FORMAT::GRIDSYNTH_CPP);
    std::string epsilon = "1e-" + std::to_string(precision);

    bool measure_time = gs_call_id % GS_CALL_PRINT_FREQUENCY == 0 || (precision >= 8);
//  bool measure_time = false;
    auto [gates_str, t_ms] = gridsynth::gridsynth_gates(
                                    fpa_str, 
                                    epsilon,
                                    NWQEC::DEFAULT_DIOPHANTINE_TIMEOUT_MS,
                                    NWQEC::DEFAULT_FACTORING_TIMEOUT_MS,
                                    false, 
                                    measure_time);

    if (gs_call_id % GS_CALL_PRINT_FREQUENCY == 0)
    {
        std::cout << "GS call: " << gs_call_id << " from thread " 
                << THREAD_ID_TO_INDEX[std::this_thread::get_id()]
                << "\n\tinputs: " << fpa_str << ", epsilon: " << epsilon
                << "\n\tgates str: " << gates_str
                << "\n\tt_ms: " << t_ms
                << "\n\tangle as float: " << convert_fpa_to_float(rotation)
                << "\n";
    }

    if (t_ms > 5000.0)
    {
        std::cerr << "[gs_cpp] possible performance issue: gridsynth took " << t_ms << " ms for inputs: " 
            << fpa_str 
            << ", epsilon: " << epsilon << " (b = " << precision 
            << "), fpa hex = " << rotation.to_hex_string() << "\n";
    }

    std::vector<INSTRUCTION::TYPE> out;
    for (char c : gates_str)
    {
        if (c == 'H')
            out.push_back(INSTRUCTION::TYPE::H);
        else if (c == 'T')
            out.push_back(INSTRUCTION::TYPE::T);
        else if (c == 'X')
            out.push_back(INSTRUCTION::TYPE::X);
        else if (c == 'Z')
            out.push_back(INSTRUCTION::TYPE::Z);
        else if (c == 'S')
            out.push_back(INSTRUCTION::TYPE::S);
    }

    [[maybe_unused]] size_t urotseq_original_size = out.size();

    // optimizations:
    rm_flip_h_subsequences(out);
    rm_consolidate_and_reduce_subsequences(out);

    [[maybe_unused]] size_t urotseq_reduced_size = out.size();

    if (gs_call_id % GS_CALL_PRINT_FREQUENCY == 0)
    {
        std::cout << "\treduced urotseq size from " << urotseq_original_size 
                << " to " << urotseq_reduced_size << "\n";
    }

    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<INSTRUCTION::TYPE>::iterator
rm_consolidate_gate(BASIS_TYPE basis, int8_t rotation_sum, std::vector<INSTRUCTION::TYPE>::iterator begin)
{
    bool is_z = basis == BASIS_TYPE::Z;
    if (rotation_sum == 0)
        return begin;  // kill the entire subsequence
    else if (rotation_sum == 1 || rotation_sum == 5)
        *begin = is_z ? INSTRUCTION::TYPE::T : INSTRUCTION::TYPE::TX;
    else if (rotation_sum == 2)
        *begin = is_z ? INSTRUCTION::TYPE::S : INSTRUCTION::TYPE::SX;
    else if (rotation_sum == 4)
        *begin = is_z ? INSTRUCTION::TYPE::Z : INSTRUCTION::TYPE::X;
    else if (rotation_sum == 6)
        *begin = is_z ? INSTRUCTION::TYPE::SDG : INSTRUCTION::TYPE::SXDG;
    else if (rotation_sum == 3 || rotation_sum == 7)
        *begin = is_z ? INSTRUCTION::TYPE::TDG : INSTRUCTION::TYPE::TXDG;
    begin++;

    // if 3 or 7, add an extra pi rotation
    if (rotation_sum == 3 || rotation_sum == 7)
    {
        *begin = is_z ? INSTRUCTION::TYPE::Z : INSTRUCTION::TYPE::X;
        begin++;
    }

    return begin;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace prog