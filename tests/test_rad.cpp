/*
 *  author: OpenAI Codex
 *  date:   10 November 2026
 * */

#include "sim/decoder_subsystem/rad.h"
#include "test_util.h"

using sampler_type = sim::ds::LognormalLatencySampler;
using rad_type = sim::ds::RADSystem<sampler_type, sampler_type>;

void test_clifford_taint_spreads_and_stops_at_first_t()
{
    rad_type rad(1000.0, 5, {1, 0}, {1, 0}, 1.0, 1, 16, 4);
    sim::Qubit q0{.qubit_id=0, .client_id=0};
    sim::Qubit q1{.qubit_id=1, .client_id=0};
    Instruction erroneous_s(Instruction::Type::S, {0});
    Instruction clean_s(Instruction::Type::S, {1});
    Instruction cx(Instruction::Type::CX, {0, 1});
    Instruction first_t(Instruction::Type::T, {1});
    Instruction second_t(Instruction::Type::T, {1});

    rad.process_event(0, 1, 1, {&q0}, &erroneous_s);
    rad.process_event(0, 1, 0, {&q1}, &clean_s);
    rad.process_event(1, 2, 0, {&q0, &q1}, &cx);
    rad.process_event(2, 3, 0, {&q1}, &first_t);
    rad.process_event(3, 4, 0, {&q1}, &second_t);
    for (int i = 0; i < 8; ++i)
        rad.l2().tick();

    CHECK(first_t.rx.verified);
    CHECK(first_t.rx.erroneous);
    CHECK(second_t.rx.verified);
    CHECK(!second_t.rx.erroneous);
}

void test_error_caught_before_t_is_corrected()
{
    rad_type rad(1000.0, 5, {1, 0}, {1, 0}, 1.0, 1, 16, 4);
    sim::Qubit q0{.qubit_id=0, .client_id=0};
    sim::Qubit q1{.qubit_id=1, .client_id=0};
    Instruction erroneous_s(Instruction::Type::S, {0});
    Instruction clean_s(Instruction::Type::S, {1});
    Instruction cx(Instruction::Type::CX, {0, 1});
    Instruction later_t(Instruction::Type::T, {1});

    rad.l2().enqueue_event(0, 1, 1, {&q0}, &erroneous_s, 0, 0, false);
    rad.l2().enqueue_event(0, 1, 0, {&q1}, &clean_s, 0, 0, true);
    rad.l2().enqueue_event(1, 2, 0, {&q0, &q1}, &cx, 0, 0, true);
    for (int i = 0; i < 8; ++i)
        rad.l2().tick();

    // L2 has caught the Clifford error before this T is issued.
    rad.l2().enqueue_event(8, 9, 0, {&q1}, &later_t, 0, 0, true);
    for (int i = 0; i < 20; ++i)
        rad.l2().tick();

    CHECK(later_t.rx.verified);
    CHECK(!later_t.rx.erroneous);
}

void test_old_taint_does_not_poison_replay()
{
    rad_type rad(1000.0, 5, {1, 0}, {1, 0}, 1.0, 1, 16, 4);
    sim::Qubit q0{.qubit_id=0, .client_id=0};
    sim::Qubit q1{.qubit_id=1, .client_id=0};
    Instruction erroneous_s(Instruction::Type::S, {0});
    Instruction clean_s(Instruction::Type::S, {1});
    Instruction first_cx(Instruction::Type::CX, {0, 1});
    Instruction first_t(Instruction::Type::T, {0});
    Instruction replay_cx(Instruction::Type::CX, {0, 1});
    Instruction replay_t(Instruction::Type::T, {0});

    rad.l2().enqueue_event(0, 1, 1, {&q0}, &erroneous_s, 0, 0, false);
    rad.l2().enqueue_event(0, 1, 0, {&q1}, &clean_s, 0, 0, true);
    rad.l2().enqueue_event(1, 2, 0, {&q0, &q1}, &first_cx, 0, 0, true);
    rad.l2().enqueue_event(2, 3, 0, {&q0}, &first_t, 0, 0, true);
    for (int i = 0; i < 8; ++i)
        rad.l2().tick();

    CHECK(first_t.rx.verified);
    CHECK(first_t.rx.erroneous);

    // q1 still carries the original taint; the later CX must not spread it.
    rad.l2().enqueue_event(8, 9, 0, {&q0, &q1}, &replay_cx, 0, 1, true);
    rad.l2().enqueue_event(9, 10, 0, {&q0}, &replay_t, 0, 1, true);
    for (int i = 0; i < 20; ++i)
        rad.l2().tick();

    CHECK(replay_t.rx.verified);
    CHECK(!replay_t.rx.erroneous);
}

int main()
{
    RUN(test_clifford_taint_spreads_and_stops_at_first_t);
    RUN(test_error_caught_before_t_is_corrected);
    RUN(test_old_taint_does_not_poison_replay);
    return qstest::finish();
}
