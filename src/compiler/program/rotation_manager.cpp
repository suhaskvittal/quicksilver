/*
    author: Suhas Vittal
    date:   06 October 2025
*/

#include "compiler/program/rotation_manager.h"
#include "nwqec/gridsynth/gridsynth.hpp"

#include "instruction_fpa_hash.inl"

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

// Platform-specific headers for thread pinning
#if defined(__linux__)
    #include <pthread.h>
    #include <sched.h>
#elif defined(_WIN32)
    #include <windows.h>
#elif defined(__APPLE__)
    #include <mach/thread_policy.h>
    #include <pthread.h>
#endif

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Helper functions and useful types:
 * */

namespace prog
{
namespace
{

struct comparable_float_type
{
    double value;
    ssize_t precision;

    bool
    operator==(const comparable_float_type& other) const
    {
        return -log10(fabsl(value - other.value))
                > std::max(static_cast<double>(precision), static_cast<double>(other.precision))-2;
    }
};

}  // anonymous namespace
}  // namespace prog


/*
 * Hash specialization for `comparable_float_type`
 * */

namespace std
{

template <>
struct hash<prog::comparable_float_type>
{
    size_t
    operator()(const prog::comparable_float_type& x) const
    {
        return std::hash<double>{}(x.value) ^ std::hash<ssize_t>{}(x.precision);
    }
};

}  // namespace std

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace prog
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

#if defined(ANGLE_USE_CFLOAT)
using angle_type = comparable_float_type;
#else
using angle_type = INSTRUCTION::fpa_type;
#endif

enum BASIS_TYPE { X, Z, NONE };

/*
 * `promise_type` stores the outcome of a scheduled synthesis.
 * Once `ready` is set, `urotseq` is valid. A promise is deleted
 * once `ref_count == 0`
 * */
struct promise_type
{
    using ptr = std::unique_ptr<promise_type>;

    bool                            ready{false};
    size_t                          ref_count{1};
    std::vector<INSTRUCTION::TYPE>  urotseq;
};

/*
 * `pending_type` is a pending synthesis request.
 * This is consumed by a thread once it hits the head
 * of `RM_PENDING` (see below).
 * */
struct pending_type
{
    using ptr = std::unique_ptr<pending_type>;

    INSTRUCTION::fpa_type rotation;
    ssize_t precision;
};

/*
 * Explanation of data structures:
 *  `RM_PENDING`: pending synthesis requests
 *  `RM_READY_MAP` a map of promises
 *  `RM_SCHED_LOCK`: mutex on all scheduling data structures shown
 *  `RM_PENDING_UPDATED` and `RM_VALUE_READY` are two condition variables for `RM_SCHED_LOCK`
 *  `RM_SIG_DONE` is used to kill all threads when cleanup starts.
 *  `RM_THREAD_DONE_COUNT` is used to block the main thread until all children die.
 *  `THREAD_ID_TO_INDEX` is used for debugging.
 * */
static std::deque<pending_type::ptr>                        RM_PENDING;
static std::unordered_map<angle_type, promise_type::ptr>    RM_READY_MAP;

static std::mutex                                           RM_SCHED_LOCK;
static std::condition_variable                              RM_PENDING_UPDATED;
static std::condition_variable                              RM_VALUE_READY;

static std::atomic<bool>                                    RM_SIG_DONE{false};

static int                                                  RM_THREAD_DONE_COUNT{0};
static std::mutex                                           RM_THREAD_DONE_LOCK;
static std::condition_variable                              RM_THREAD_DONE_COUNT_UPDATED;

static std::unordered_map<std::thread::id, size_t>          THREAD_ID_TO_INDEX;

/*
 * Gridsynth debug info is printed periodically according to `GS_CALL_PRINT_FREQUENCY`
 * */
constexpr size_t           GS_CALL_PRINT_FREQUENCY = 100'000;
static std::atomic<size_t> GS_CALL_COUNT{0};

/*
 * Converts the given FPA to either (1) an FPA, or (2) a float (see `angle_type` above).
 * */
angle_type _make_angle(const INSTRUCTION::fpa_type&, ssize_t precision);

/*
 * Returns the basis (X or Z or None) for the given gate.
 * */
constexpr BASIS_TYPE _get_basis_type(INSTRUCTION::TYPE g);

/*
 * Flips the basis of the given gate. For example, T --> TX or vice versa.
 * */
constexpr INSTRUCTION::TYPE _flip_basis(INSTRUCTION::TYPE g);

/*
 * `_get_rotation_value` quantizes the "rotation" of `g` to a 3-bit value.
 * 1 = pi/4 rotation (T-like gates),
 * 2 = pi/2 rotation (S-like gates)
 * 4 = pi rotations (X or Z)
 * */
constexpr int8_t _get_rotation_value(INSTRUCTION::TYPE g);

/*
 * Pins the calling thread to a specific logical core.
 * Returns true on success, false if platform unsupported or operation fails.
 * */
bool _pin_thread_to_core(size_t core_id);

/*
 * In `_thread_iteration`, a thread will start synthesizing some pending
 * synthesis request.
 * */
void _thread_iteration();

/*
 * Converts a rotation gate into a Clifford+T gate sequence.
 * */
std::vector<INSTRUCTION::TYPE> _synthesize_rotation(const INSTRUCTION::fpa_type&, ssize_t precision);

/*
 * Flips the basis of all gates sandwiched by two H gates. For example, H*T*S*H --> TX*SX
 * */
void _flip_h_subsequences(std::vector<INSTRUCTION::TYPE>&);

/*
 * Consolidation involves merging gates in the same basis into one or two gates (at most
 * one non-software gate).
 * */
void _consolidate_and_reduce_subsequences(std::vector<INSTRUCTION::TYPE>&);

/*
 * This function overwrites the data starting from `begin` inplace (by modifying the gate types).
 * Then, it returns an iterator right after the last modified entry.
 * */
std::vector<INSTRUCTION::TYPE>::iterator _consolidate_gate(BASIS_TYPE, 
                                                            int8_t rotation_sum,
                                                            std::vector<INSTRUCTION::TYPE>::iterator begin);

/*
 * This is just a utility function for printing information
 * */
template <class ITERABLE>
std::string _urotseq_to_string(ITERABLE iterable);

}  // anonymous namespace

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
rotation_manager_init(size_t num_threads)
{
    RM_SIG_DONE.store(false);

    // spawn `num_threads` and detach them:
    for (size_t i = 0; i < num_threads; i++)
    {
        std::thread th{ [i] ()
        {
            // Pin thread to core (wraps around if more threads than cores)
            size_t core_id = i % std::thread::hardware_concurrency();
            _pin_thread_to_core(core_id);

            while (!RM_SIG_DONE.load())
                _thread_iteration();
        }};
        THREAD_ID_TO_INDEX[th.get_id()] = i;
        th.detach();
    }

    std::lock_guard<std::mutex> child_lock(RM_THREAD_DONE_LOCK);
    RM_THREAD_DONE_COUNT = num_threads;
}

void
rotation_manager_end(bool block)
{
    /*
     * 1. first signal that the rotation manager is done.
     * */
    std::unique_lock<std::mutex> sched_lock(RM_SCHED_LOCK);
    RM_SIG_DONE.store(true);
    RM_PENDING_UPDATED.notify_all();
    sched_lock.unlock();

    /*
     * 2. if `block` is true, then wait for all threads to die before exiting this function
     * */
    if (block)
    {
        std::unique_lock<std::mutex> child_lock(RM_THREAD_DONE_LOCK);
        while (RM_THREAD_DONE_COUNT > 0)
            RM_THREAD_DONE_COUNT_UPDATED.wait(child_lock);
    }

    if (RM_READY_MAP.size() > 0)
        std::cerr << "rotation_manager_end: RM_READY_MAP still has " << RM_READY_MAP.size() << " entries\n";
    RM_READY_MAP.clear();

    if (RM_PENDING.size() > 0)
        std::cerr << "rotation_manager_end: RM_PENDING still has " << RM_PENDING.size() << " entries\n";
    RM_PENDING.clear();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
rotation_manager_schedule_synthesis(const INSTRUCTION::fpa_type& rotation, ssize_t precision)
{
    std::lock_guard<std::mutex> sched_lock(RM_SCHED_LOCK);
    pending_type::ptr p = std::make_unique<pending_type>(rotation, precision);
    RM_PENDING.push_back(std::move(p));
    RM_PENDING_UPDATED.notify_one();
}

std::vector<INSTRUCTION::TYPE>
rotation_manager_find(const INSTRUCTION::fpa_type& rotation, ssize_t precision)
{
    auto k = _make_angle(rotation, precision);

    std::unique_lock<std::mutex> sched_lock(RM_SCHED_LOCK);

rm_get_entry:
    auto it = RM_READY_MAP.find(k);
    if (it == RM_READY_MAP.end() || !it->second->ready)
    {
        RM_VALUE_READY.wait(sched_lock);
        goto rm_get_entry;
    }

    promise_type::ptr& p = it->second;
    auto urotseq = p->urotseq;

    p->ref_count--;
    if (p->ref_count == 0)
        RM_READY_MAP.erase(it);
    sched_lock.unlock();

    return urotseq;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/* HELPER FUNCTIONS START HERE */

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

angle_type
_make_angle(const INSTRUCTION::fpa_type& rotation, ssize_t precision)
{
#if defined(ANGLE_USE_CFLOAT)
    return comparable_float_type{convert_fpa_to_float(rotation), precision};
#else
    return INSTRUCTION::fpa_type(rotation);
#endif
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
_pin_thread_to_core(size_t core_id)
{
#if defined(__linux__)
    // Linux: Use pthread_setaffinity_np with cpu_set_t
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();
    int result = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
    return result == 0;

#elif defined(_WIN32)
    // Windows: Use SetThreadAffinityMask with bitmask
    DWORD_PTR mask = 1ULL << core_id;
    DWORD_PTR result = SetThreadAffinityMask(GetCurrentThread(), mask);
    return result != 0;

#elif defined(__APPLE__)
    // macOS: Use thread_policy_set with THREAD_AFFINITY_POLICY
    /*
    thread_affinity_policy_data_t policy;
    policy.affinity_tag = static_cast<integer_t>(core_id);

    kern_return_t result = thread_policy_set(
        pthread_mach_thread_np(pthread_self()),
        THREAD_AFFINITY_POLICY,
        reinterpret_cast<thread_policy_t>(&policy),
        THREAD_AFFINITY_POLICY_COUNT
    );
    return result == KERN_SUCCESS;
    */
    return true;

#else
    // Unsupported platform
    (void)core_id;  // Suppress unused parameter warning
    return false;
#endif
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

constexpr BASIS_TYPE
_get_basis_type(INSTRUCTION::TYPE g)
{
    switch (g)
    {
    case INSTRUCTION::TYPE::X:
    case INSTRUCTION::TYPE::SX:
    case INSTRUCTION::TYPE::SXDG:
    case INSTRUCTION::TYPE::TX:
    case INSTRUCTION::TYPE::TXDG:
        return BASIS_TYPE::X;

    case INSTRUCTION::TYPE::Z:
    case INSTRUCTION::TYPE::S:
    case INSTRUCTION::TYPE::SDG:
    case INSTRUCTION::TYPE::T:
    case INSTRUCTION::TYPE::TDG:
        return BASIS_TYPE::Z;

    default:
        return BASIS_TYPE::NONE;
    }
}

constexpr INSTRUCTION::TYPE
_flip_basis(INSTRUCTION::TYPE g)
{
    switch (g)
    {
    case INSTRUCTION::TYPE::Z:
        return INSTRUCTION::TYPE::X;
    case INSTRUCTION::TYPE::S:
        return INSTRUCTION::TYPE::SX;
    case INSTRUCTION::TYPE::SDG:
        return INSTRUCTION::TYPE::SXDG;
    case INSTRUCTION::TYPE::T:
        return INSTRUCTION::TYPE::TX;
    case INSTRUCTION::TYPE::TDG:
        return INSTRUCTION::TYPE::TXDG;

    case INSTRUCTION::TYPE::X:
        return INSTRUCTION::TYPE::Z;
    case INSTRUCTION::TYPE::SX:
        return INSTRUCTION::TYPE::S;
    case INSTRUCTION::TYPE::SXDG:
        return INSTRUCTION::TYPE::SDG;
    case INSTRUCTION::TYPE::TX:
        return INSTRUCTION::TYPE::T;
    case INSTRUCTION::TYPE::TXDG:
        return INSTRUCTION::TYPE::TDG;

    default:
        return g;
    }
}

constexpr int8_t
_get_rotation_value(INSTRUCTION::TYPE g)
{
    // the output is r, where g is some rotation of r*pi/4
    switch (g)
    {
    case INSTRUCTION::TYPE::X:
    case INSTRUCTION::TYPE::Z:
        return 4;
    case INSTRUCTION::TYPE::S:
    case INSTRUCTION::TYPE::SX:
        return 2;
    case INSTRUCTION::TYPE::SDG:
    case INSTRUCTION::TYPE::SXDG:
        return 6;
    case INSTRUCTION::TYPE::T:
    case INSTRUCTION::TYPE::TX:
        return 1;
    case INSTRUCTION::TYPE::TDG:
    case INSTRUCTION::TYPE::TXDG:
        return 7;
    default:
        return -1;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
_thread_iteration()
{
    // critical section: get pending rotation request:
    std::unique_lock<std::mutex> sched_lock(RM_SCHED_LOCK);
    while (RM_PENDING.empty() && !RM_SIG_DONE.load())
        RM_PENDING_UPDATED.wait(sched_lock);

    if (RM_SIG_DONE.load())
    {
        sched_lock.unlock();

        std::lock_guard<std::mutex> child_lock(RM_THREAD_DONE_LOCK);
        RM_THREAD_DONE_COUNT--;
        RM_THREAD_DONE_COUNT_UPDATED.notify_one();

        return;
    }

    pending_type::ptr entry = std::move(RM_PENDING.front());
    RM_PENDING.pop_front();
    auto k = _make_angle(entry->rotation, entry->precision);
    size_t ref_count = 1;

    // insert into `RM_READY_MAP`:
    auto it = RM_READY_MAP.find(k);
    if (it == RM_READY_MAP.end())
    {
        promise_type::ptr p = std::make_unique<promise_type>();
        p->ref_count = ref_count;
        promise_type* p_raw = p.get();  // need this after we move `p`
        RM_READY_MAP.insert({k, std::move(p)});
        sched_lock.unlock();

        p_raw->urotseq = _synthesize_rotation(entry->rotation, entry->precision);

        sched_lock.lock();
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

std::vector<INSTRUCTION::TYPE>
_synthesize_rotation(const INSTRUCTION::fpa_type& rotation, ssize_t precision)
{
    size_t gs_call_id = GS_CALL_COUNT.fetch_add(1);

    // call gridsynth:
    std::string fpa_str = fpa::to_string(rotation, fpa::STRING_FORMAT::GRIDSYNTH_CPP);
    std::string epsilon = "1e-" + std::to_string(precision);

    bool measure_time = (gs_call_id % GS_CALL_PRINT_FREQUENCY == 0) || (precision >= 8);
    auto [gates_str, t_ms] = gridsynth::gridsynth_gates(
                                    fpa_str,
                                    epsilon,
                                    NWQEC::DEFAULT_DIOPHANTINE_TIMEOUT_MS,
                                    NWQEC::DEFAULT_FACTORING_TIMEOUT_MS,
                                    false,
                                    measure_time);

#if defined(RM_VERBOSE)
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
#endif

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
    _flip_h_subsequences(out);
    _consolidate_and_reduce_subsequences(out);
    [[maybe_unused]] size_t urotseq_reduced_size = out.size();

#if defined(RM_VERBOSE)
    if (gs_call_id % GS_CALL_PRINT_FREQUENCY == 0)
    {
        std::cout << "\treduced urotseq size from " << urotseq_original_size
                << " to " << urotseq_reduced_size << "\n";
        std::cout << "final sequence =";
        for (auto t : out)
            std::cout << " " << BASIS_GATES[static_cast<int>(t)];
        std::cout << "\n";
    }
#endif

    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
_flip_h_subsequences(std::vector<INSTRUCTION::TYPE>& urotseq)
{
    size_t h_count = std::count(urotseq.begin(), urotseq.end(), INSTRUCTION::TYPE::H);

    auto begin = urotseq.begin();
    // while there are at least two H gates, flip the subsequence between them:
    while (h_count >= 2)
    {
        auto h_begin = std::find(begin, urotseq.end(), INSTRUCTION::TYPE::H);
        if (h_begin == urotseq.end())
            break;

        auto h_end = std::find(h_begin+1, urotseq.end(), INSTRUCTION::TYPE::H);
        if (h_end == urotseq.end())
            break;

        std::for_each(h_begin+1, h_end, [](auto& g) { g = _flip_basis(g); });

        // set the H gates to nil -- we will remove all NIL gates at the end:
        *h_begin = INSTRUCTION::TYPE::NIL;
        *h_end = INSTRUCTION::TYPE::NIL;

        begin = h_end+1;
        h_count -= 2;
    }

    if (h_count == 1)
    {
        // the last H gate can be propagated to the end by flipping everything between:
        auto h_begin = std::find(begin, urotseq.end(), INSTRUCTION::TYPE::H);
        std::for_each(h_begin+1, urotseq.end(), [] (auto& g) { g = _flip_basis(g); });
        std::move(h_begin+1, urotseq.end(), h_begin);
        urotseq.back() = INSTRUCTION::TYPE::H;
    }

    auto it = std::remove_if(urotseq.begin(), urotseq.end(), [] (const auto& inst) { return inst == INSTRUCTION::TYPE::NIL; });
    urotseq.erase(it, urotseq.end());
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
_consolidate_and_reduce_subsequences(std::vector<INSTRUCTION::TYPE>& urotseq)
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
            if (_get_basis_type(g) != current_basis)
            {
                // replace the sequence with the appropriate gate:
                auto seq_kill_begin = _consolidate_gate(current_basis, current_rotation_sum, seq_begin);
                // set all gates from `seq_kill_begin` to `it` to `NIL` -- we will remove these later:
                std::fill(seq_kill_begin, it, INSTRUCTION::TYPE::NIL);

                // set basis type to none since we can now start a new subsequence
                current_basis = BASIS_TYPE::NONE;
                current_rotation_sum = 0;
            }
            else
            {
                current_rotation_sum += _get_rotation_value(g);
                current_rotation_sum &= 7;  // mod 8
            }
        }

        // this is not an else since we may set `current_basis` to `BASIS_TYPE::NONE` in the above if statement
        if (current_basis == BASIS_TYPE::NONE)
        {
            if (g == INSTRUCTION::TYPE::H)
                continue;  // nothing to be done

            current_basis = _get_basis_type(g);
            if (current_basis == BASIS_TYPE::NONE)
                throw std::runtime_error("invalid gate: " + std::string(BASIS_GATES[static_cast<size_t>(g)]));
            current_rotation_sum = _get_rotation_value(g);
            seq_begin = it;
        }
    }

    // if we are still in a subsequence, finish it off:
    if (current_basis != BASIS_TYPE::NONE)
    {
        auto seq_kill_begin = _consolidate_gate(current_basis, current_rotation_sum, seq_begin);
        std::fill(seq_kill_begin, urotseq.end(), INSTRUCTION::TYPE::NIL);
    }

    // remove all `NIL` gates:
    auto it = std::remove_if(urotseq.begin(), urotseq.end(), [] (auto t) { return t == INSTRUCTION::TYPE::NIL; });
    urotseq.erase(it, urotseq.end());
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<INSTRUCTION::TYPE>::iterator
_consolidate_gate(BASIS_TYPE basis, int8_t rotation_sum, std::vector<INSTRUCTION::TYPE>::iterator begin)
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
    if (rotation_sum == 5 || rotation_sum == 3)
    {
        *begin = is_z ? INSTRUCTION::TYPE::Z : INSTRUCTION::TYPE::X;
        begin++;
    }

    return begin;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// Template helper function definition
template <class ITERABLE> std::string
_urotseq_to_string(ITERABLE iterable)
{
    std::stringstream strm;
    bool first{true};
    for (auto x : iterable)
    {
        if (!first)
            strm << "'";
        first = false;
        std::string_view sx = BASIS_GATES[static_cast<size_t>(x)];
        strm << sx;
    }
    return strm.str();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // anonymous namespace

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace prog
