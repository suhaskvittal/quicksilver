/*
 *  author: Claude (Anthropic)
 *
 *  Validation tests for `StallMonitor<N,T>` in `sim/stall_monitor.{h,tpp}`.
 *
 *  A StallMonitor records, over half-open cycle ranges [start, end), which stall
 *  types are active (as a bitmask). Overlapping ranges of different types OR
 *  their flags together. On commit:
 *    - a cycle with exactly ONE active type is an "isolated" stall for that type
 *      (counted per type via isolated_stalls_for),
 *    - a cycle with >= 1 active type counts once toward cycles_with_stalls().
 *  Stats are only updated when ranges are committed (commit_contents, or an
 *  eviction once more than max_ranges ranges are buffered).
 * */

#include "sim/stall_monitor.h"
#include "test_util.h"

#include <cstdint>

using namespace sim;

namespace
{

// Three stall types; N = 3.
enum class Stall { A = 0, B = 1, C = 2 };
using SM = StallMonitor<3, Stall>;

constexpr size_t BIG{64};   // max_ranges large enough to avoid eviction

}  // anonymous namespace

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * A single range of one type: every cycle is an isolated stall for that type.
 * */
void
test_single_range_isolated()
{
    SM sm(BIG);
    sm.add_stall_range(Stall::A, 0, 10, false);   // [0,10)
    sm.commit_contents();

    CHECK_EQ(sm.cycles_with_stalls(),        uint64_t{10});
    CHECK_EQ(sm.isolated_stalls_for(Stall::A), uint64_t{10});
    CHECK_EQ(sm.isolated_stalls_for(Stall::B), uint64_t{0});
    CHECK_EQ(sm.isolated_stalls_for(Stall::C), uint64_t{0});
}

/*
 * The `inclusive` flag treats [start, end] as [start, end+1).
 * */
void
test_inclusive_flag()
{
    SM sm(BIG);
    sm.add_stall_range(Stall::A, 0, 4, true);   // inclusive [0,4] -> [0,5)
    sm.commit_contents();

    CHECK_EQ(sm.cycles_with_stalls(),          uint64_t{5});
    CHECK_EQ(sm.isolated_stalls_for(Stall::A), uint64_t{5});
}

/*
 * Empty range (start >= end) is a no-op.
 * */
void
test_empty_range_noop()
{
    SM sm(BIG);
    sm.add_stall_range(Stall::A, 5, 5, false);
    sm.add_stall_range(Stall::A, 8, 4, false);
    sm.commit_contents();

    CHECK_EQ(sm.cycles_with_stalls(),          uint64_t{0});
    CHECK_EQ(sm.isolated_stalls_for(Stall::A), uint64_t{0});
}

/*
 * Disjoint ranges: same type accumulates isolated cycles; different types are
 * each isolated over their own cycles, and all count toward the total.
 * */
void
test_disjoint_ranges()
{
    SM same(BIG);
    same.add_stall_range(Stall::A, 0, 5, false);
    same.add_stall_range(Stall::A, 10, 15, false);
    same.commit_contents();
    CHECK_EQ(same.isolated_stalls_for(Stall::A), uint64_t{10});
    CHECK_EQ(same.cycles_with_stalls(),          uint64_t{10});

    SM diff(BIG);
    diff.add_stall_range(Stall::A, 0, 5, false);
    diff.add_stall_range(Stall::B, 10, 15, false);
    diff.commit_contents();
    CHECK_EQ(diff.isolated_stalls_for(Stall::A), uint64_t{5});
    CHECK_EQ(diff.isolated_stalls_for(Stall::B), uint64_t{5});
    CHECK_EQ(diff.cycles_with_stalls(),          uint64_t{10});
}

/*
 * Partially overlapping ranges of different types: the overlap region has both
 * types set (not isolated), the non-overlapping tails are isolated, and every
 * cycle in the union counts once toward the total.
 *
 *   A: [0,10)   B: [5,15)
 *   -> [0,5) A only | [5,10) A|B | [10,15) B only
 * */
void
test_overlap_partial()
{
    SM sm(BIG);
    sm.add_stall_range(Stall::A, 0, 10, false);
    sm.add_stall_range(Stall::B, 5, 15, false);
    sm.commit_contents();

    CHECK_EQ(sm.isolated_stalls_for(Stall::A), uint64_t{5});    // [0,5)
    CHECK_EQ(sm.isolated_stalls_for(Stall::B), uint64_t{5});    // [10,15)
    CHECK_EQ(sm.cycles_with_stalls(),          uint64_t{15});   // union [0,15)
}

/*
 * A later range fully contained inside an earlier one of another type splits
 * the earlier range into two isolated tails around a combined middle.
 *
 *   A: [0,10)   B: [2,4)
 *   -> [0,2) A | [2,4) A|B | [4,10) A
 * */
void
test_overlap_subset()
{
    SM sm(BIG);
    sm.add_stall_range(Stall::A, 0, 10, false);
    sm.add_stall_range(Stall::B, 2, 4, false);
    sm.commit_contents();

    CHECK_EQ(sm.isolated_stalls_for(Stall::A), uint64_t{8});    // [0,2) + [4,10)
    CHECK_EQ(sm.isolated_stalls_for(Stall::B), uint64_t{0});    // only ever combined
    CHECK_EQ(sm.cycles_with_stalls(),          uint64_t{10});
}

/*
 * Three types over the same cycles: no isolated stalls, but the cycles still
 * count once toward the total.
 * */
void
test_full_overlap_all_types()
{
    SM sm(BIG);
    sm.add_stall_range(Stall::A, 0, 10, false);
    sm.add_stall_range(Stall::B, 0, 10, false);
    sm.add_stall_range(Stall::C, 0, 10, false);
    sm.commit_contents();

    CHECK_EQ(sm.isolated_stalls_for(Stall::A), uint64_t{0});
    CHECK_EQ(sm.isolated_stalls_for(Stall::B), uint64_t{0});
    CHECK_EQ(sm.isolated_stalls_for(Stall::C), uint64_t{0});
    CHECK_EQ(sm.cycles_with_stalls(),          uint64_t{10});
}

/*
 * Stats are only realized on commit: querying before commit_contents sees zero.
 * */
void
test_stats_require_commit()
{
    SM sm(BIG);
    sm.add_stall_range(Stall::A, 0, 10, false);

    CHECK_EQ(sm.cycles_with_stalls(),          uint64_t{0});   // buffered, not committed
    CHECK_EQ(sm.isolated_stalls_for(Stall::A), uint64_t{0});

    sm.commit_contents();
    CHECK_EQ(sm.cycles_with_stalls(),          uint64_t{10});
    CHECK_EQ(sm.isolated_stalls_for(Stall::A), uint64_t{10});
}

/*
 * When more than max_ranges ranges are buffered, the earliest are committed
 * (evicted) on the fly. The eviction must not lose or double-count cycles.
 *
 * Four non-adjacent disjoint A ranges of 1 cycle each, with max_ranges = 2:
 * two get evicted during add, two remain for commit_contents. Total must be 4.
 * */
void
test_eviction_preserves_counts()
{
    SM sm(2);
    sm.add_stall_range(Stall::A, 0, 1, false);
    sm.add_stall_range(Stall::A, 2, 3, false);
    sm.add_stall_range(Stall::A, 4, 5, false);   // buffer exceeds 2 -> evict [0,1)
    sm.add_stall_range(Stall::A, 6, 7, false);   // evict [2,3)
    sm.commit_contents();                        // commit [4,5), [6,7)

    CHECK_EQ(sm.isolated_stalls_for(Stall::A), uint64_t{4});
    CHECK_EQ(sm.cycles_with_stalls(),          uint64_t{4});
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main()
{
    RUN(test_single_range_isolated);
    RUN(test_inclusive_flag);
    RUN(test_empty_range_noop);
    RUN(test_disjoint_ranges);
    RUN(test_overlap_partial);
    RUN(test_overlap_subset);
    RUN(test_full_overlap_all_types);
    RUN(test_stats_require_commit);
    RUN(test_eviction_preserves_counts);
    return qstest::finish();
}
