/*
 *  author: Claude (Anthropic)
 *
 *  Validation tests for the routing `Resource` interval logic in
 *  `sim/routing.{h,cpp}`.
 *
 *  A Resource tracks locked time intervals as half-open ranges [a, b): locking
 *  [a, b) occupies cycles a .. b-1, so [0,5) and [5,10) are NOT in conflict.
 *  (`_check_for_intersection` decrements both range ends to enforce this.)
 * */

#include "sim/routing.h"
#include "test_util.h"

using namespace sim::routing;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * is_lockable reports conflicts against half-open ranges: touching intervals
 * are fine, any shared cycle is a conflict.
 * */
void
test_is_lockable_half_open()
{
    Resource r;
    CHECK(r.is_lockable(0, 10));   // empty resource: anything goes

    r.lock_for_time_interval(5, 10);   // occupy cycles 5..9

    // adjacency is allowed (half-open)
    CHECK(r.is_lockable(0, 5));     // ends exactly where the lock starts
    CHECK(r.is_lockable(10, 15));   // starts exactly where the lock ends
    CHECK(r.is_lockable(0, 4));     // strictly before, with a gap
    CHECK(r.is_lockable(11, 20));   // strictly after, with a gap

    // any overlap is rejected
    CHECK(!r.is_lockable(5, 10));   // identical
    CHECK(!r.is_lockable(4, 6));    // partial overlap at cycle 5
    CHECK(!r.is_lockable(7, 9));    // fully contained
    CHECK(!r.is_lockable(0, 20));   // fully contains the lock
}

/*
 * lock_for_time_interval accepts disjoint ranges in any order and keeps them
 * queryable; the gaps between them stay lockable.
 * */
void
test_lock_disjoint_out_of_order()
{
    Resource r;
    r.lock_for_time_interval(20, 25);
    r.lock_for_time_interval(0, 5);
    r.lock_for_time_interval(10, 15);

    // each locked region is occupied
    CHECK(!r.is_lockable(0, 5));
    CHECK(!r.is_lockable(10, 15));
    CHECK(!r.is_lockable(20, 25));

    // the gaps between them are free
    CHECK(r.is_lockable(5, 10));
    CHECK(r.is_lockable(15, 20));
    CHECK(r.is_lockable(25, 30));

    // a range spanning two locks is rejected
    CHECK(!r.is_lockable(3, 12));
}

/*
 * next_ready_cycle finds the earliest cycle >= current at which a duration-t
 * lock fits. Clear-gap cases.
 * */
void
test_next_ready_cycle_basic()
{
    Resource empty;
    CHECK_EQ(empty.next_ready_cycle(7, 4), cycle_type{7});   // nothing in the way

    Resource r;
    r.lock_for_time_interval(5, 10);   // occupy 5..9

    CHECK_EQ(r.next_ready_cycle(0, 3),  cycle_type{0});    // [0,3) fits before the lock
    CHECK_EQ(r.next_ready_cycle(6, 3),  cycle_type{10});   // start sits inside the lock -> jump to 10
    CHECK_EQ(r.next_ready_cycle(12, 3), cycle_type{12});   // start already past the lock
}

/*
 * Whatever cycle next_ready_cycle returns must actually be lockable for the
 * requested duration. (This invariant holds even where next_ready_cycle is
 * over-conservative.)
 * */
void
test_next_ready_cycle_returns_lockable()
{
    Resource r;
    r.lock_for_time_interval(5, 10);
    r.lock_for_time_interval(20, 25);

    struct { cycle_type cur, t; } cases[] = {
        {0, 3}, {0, 5}, {0, 10}, {6, 10}, {18, 2}, {12, 4}, {24, 6},
    };
    for (auto c : cases)
    {
        const cycle_type rc = r.next_ready_cycle(c.cur, c.t);
        CHECK(rc >= c.cur);
        CHECK(r.is_lockable(rc, rc + c.t));
    }
}

/*
 * next_ready_cycle should return the EARLIEST lockable cycle. With a lock on
 * [5,10), a duration-5 request from cycle 0 fits exactly at [0,5) (adjacency is
 * lockable, as test_is_lockable_half_open confirms), so the answer must be 0.
 * This is the half-open boundary case: next_ready_cycle needs `c+t <= a`, not
 * a strict `<`.
 * */
void
test_next_ready_cycle_earliest_adjacent()
{
    Resource r;
    r.lock_for_time_interval(5, 10);

    CHECK(r.is_lockable(0, 5));                        // the slot really is free
    CHECK_EQ(r.next_ready_cycle(0, 5), cycle_type{0}); // ... so this must be 0
}

/*
 * A Resource tracks at most RANGE_LIMIT (8) ranges. Locking a 9th (later) range
 * evicts the earliest one, which then stops being tracked (its cycles read as
 * lockable again).
 * */
void
test_range_limit_eviction()
{
    Resource r;
    for (cycle_type i = 0; i < 16; i += 2)   // 8 ranges: [0,1),[2,3),...,[14,15)
        r.lock_for_time_interval(i, i + 1);

    CHECK(!r.is_lockable(0, 1));    // earliest range tracked
    CHECK(!r.is_lockable(14, 15));  // latest range tracked

    r.lock_for_time_interval(16, 17);   // 9th range -> evicts [0,1)

    CHECK(r.is_lockable(0, 1));     // evicted: no longer tracked
    CHECK(!r.is_lockable(2, 3));    // still tracked
    CHECK(!r.is_lockable(14, 15));  // still tracked
    CHECK(!r.is_lockable(16, 17));  // newly tracked
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main()
{
    RUN(test_is_lockable_half_open);
    RUN(test_lock_disjoint_out_of_order);
    RUN(test_next_ready_cycle_basic);
    RUN(test_next_ready_cycle_returns_lockable);
    RUN(test_next_ready_cycle_earliest_adjacent);
    RUN(test_range_limit_eviction);
    return qstest::finish();
}
