/*
 *  author: Claude (Anthropic)
 *
 *  Validation tests for the clock/frequency machinery in `sim/operable.h` and
 *  `sim/operable.cpp`:
 *    - compute_freq_khz
 *    - convert_cycles_between_frequencies<T>
 *    - convert_cycles_to_time_ns / convert_time_ns_to_cycles
 *    - coordinate_clock_scale (observed indirectly via tick() cadence)
 *    - fast_forward_all_operables_to_time_ns
 * */

#include "sim/operable.h"
#include "test_util.h"

#include <cstdint>
#include <vector>

using namespace sim;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

/*
 * Minimal concrete Operable that counts how many times operate() runs. Every
 * call reports progress (returns 1) so tick() never trips the deadlock guard.
 * */
class CountingOperable : public Operable
{
public:
    explicit CountingOperable(double freq_khz) : Operable("op", freq_khz) {}
    long operate_calls{0};
protected:
    long operate() override { return ++operate_calls, 1L; }
};

}  // anonymous namespace

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * compute_freq_khz(period_ns) == 1e6 / period_ns.
 * */
void
test_compute_freq_khz()
{
    CHECK_NEAR(compute_freq_khz(1000), 1000.0, 1e-9);   // 1 MHz
    CHECK_NEAR(compute_freq_khz(1),    1e6,    1e-3);
    CHECK_NEAR(compute_freq_khz(1200), 1e6 / 1200.0, 1e-9);
}

/*
 * convert_cycles_between_frequencies<T>(cycles, f1, f2) == cycles * (f2/f1),
 * with a ceil applied only for integral T.
 * */
void
test_convert_cycles_between_frequencies()
{
    // identity when the two frequencies match
    CHECK_EQ(convert_cycles_between_frequencies<uint64_t>(7, 5.0, 5.0), uint64_t{7});
    CHECK_NEAR(convert_cycles_between_frequencies<double>(7.0, 5.0, 5.0), 7.0, 1e-9);

    // slowing down (f2 < f1): fewer cycles; integral result is rounded up
    CHECK_EQ(convert_cycles_between_frequencies<uint64_t>(10, 2.0, 1.0), uint64_t{5});   // exact 5
    CHECK_EQ(convert_cycles_between_frequencies<uint64_t>(10, 3.0, 1.0), uint64_t{4});   // ceil(3.33)
    CHECK_NEAR(convert_cycles_between_frequencies<double>(10.0, 3.0, 1.0), 10.0 / 3.0, 1e-9);

    // speeding up (f2 > f1): more cycles
    CHECK_EQ(convert_cycles_between_frequencies<uint64_t>(10, 1.0, 2.0), uint64_t{20});
}

/*
 * convert_cycles_to_time_ns and convert_time_ns_to_cycles are inverses (up to
 * the rounding each applies).
 * */
void
test_convert_cycles_time_roundtrip()
{
    // 1000 cycles at 1000 kHz (1 MHz => 1000 ns/cycle) = 1,000,000 ns
    CHECK_EQ(convert_cycles_to_time_ns(1000, 1000.0), uint64_t{1000000});
    CHECK_EQ(convert_time_ns_to_cycles(1000000, 1000.0), cycle_type{1000});

    // round-trip cycles -> ns -> cycles must reproduce the original count
    // exactly (convert_time_ns_to_cycles must not over-provision on an exact
    // number of cycles).
    for (cycle_type c : {cycle_type{1}, cycle_type{7}, cycle_type{1000}, cycle_type{54321}})
    {
        const uint64_t   t_ns = convert_cycles_to_time_ns(c, 1000.0);
        const cycle_type back = convert_time_ns_to_cycles(t_ns, 1000.0);
        CHECK_EQ(back, c);
    }
}

/*
 * coordinate_clock_scale sets each operable's clock_scale_ to
 * (max_freq / freq) - 1.0 (private, so we observe it through tick() cadence):
 * the fastest operable advances one cycle per tick, an f-times-slower operable
 * advances one cycle every f ticks.
 * */
void
test_coordinate_clock_scale_cadence()
{
    // 2:1 ratio -- slow advances every other tick
    CountingOperable fast(2000.0), slow(1000.0);
    coordinate_clock_scale({&fast, &slow});
    for (int i = 0; i < 10; i++) { fast.tick(); slow.tick(); }
    CHECK_EQ(fast.current_cycle(), cycle_type{10});
    CHECK_EQ(slow.current_cycle(), cycle_type{5});

    // 3:1 ratio -- slow advances every third tick
    CountingOperable f3(3000.0), s3(1000.0);
    coordinate_clock_scale({&f3, &s3});
    for (int i = 0; i < 9; i++) { f3.tick(); s3.tick(); }
    CHECK_EQ(f3.current_cycle(), cycle_type{9});
    CHECK_EQ(s3.current_cycle(), cycle_type{3});

    // a lone operable is its own fastest -> advances one cycle per tick
    CountingOperable solo(1234.0);
    coordinate_clock_scale({&solo});
    for (int i = 0; i < 6; i++) solo.tick();
    CHECK_EQ(solo.current_cycle(), cycle_type{6});
}

/*
 * fast_forward_all_operables_to_time_ns sets each operable's current cycle to
 * convert_time_ns_to_cycles(target, its freq).
 * */
void
test_fast_forward()
{
    CountingOperable fast(2000.0), slow(1000.0);
    const uint64_t target_ns = 1000000;   // 1 ms
    fast_forward_all_operables_to_time_ns({&fast, &slow}, target_ns);

    CHECK_EQ(fast.current_cycle(), convert_time_ns_to_cycles(target_ns, 2000.0));   // 2000
    CHECK_EQ(slow.current_cycle(), convert_time_ns_to_cycles(target_ns, 1000.0));   // 1000
    CHECK_EQ(fast.current_cycle(), cycle_type{2000});
    CHECK_EQ(slow.current_cycle(), cycle_type{1000});
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main()
{
    RUN(test_compute_freq_khz);
    RUN(test_convert_cycles_between_frequencies);
    RUN(test_convert_cycles_time_roundtrip);
    RUN(test_coordinate_clock_scale_cadence);
    RUN(test_fast_forward);
    return qstest::finish();
}
