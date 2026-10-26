#include "sim/compute_subsystem.h"
#include "sim/memory/remote_memory.h"
#include "sim/qubit.h"
#include "test_util.h"

using namespace sim;

namespace
{
constexpr size_t D = 6;
constexpr cycle_type PERIOD = 4*D;
using EPRMemory = RemoteMemory<BBMemory>;

void advance_to(std::vector<Operable*> ops, cycle_type cycle)
{
    while (ops.front()->current_cycle() < cycle)
        for (auto* op : ops)
            op->tick();
}

void test_production_fast_forward_matches_ticks()
{
    for (bool distillation : {false, true})
    {
        BulkProducer stepped("stepped", 1000.0), skipped("skipped", 1000.0);
        for (auto* p : {&stepped, &skipped})
        {
            if (distillation)
            {
                p->add_level(ed_protocol_2a(EDImplMethod::DISAGGREGATED_TYPE_A), 8);
                p->add_level(ed_protocol_2b(EDImplMethod::DISAGGREGATED_TYPE_A), 4);
            }
            else
                p->add_level(ideal_epr_distribution(D), 1);
        }
        for (cycle_type target : {1UL, 24UL, 25UL, 49UL, 200UL, 1000UL, 10000UL, 10037UL, 20000UL})
        {
            advance_to({&stepped}, target);
            fast_forward_all_operables_to_time_ns({&skipped}, convert_cycles_to_time_ns(target, 1000.0));
            CHECK_EQ(stepped.current_cycle(), skipped.current_cycle());
            for (size_t i = 0; i < stepped.level_count(); ++i)
            {
                const auto& a = stepped.level(i);
                const auto& b = skipped.level(i);
                CHECK_EQ(a.unified_buffer_occupancy, b.unified_buffer_occupancy);
                CHECK_EQ(a.s_completions, b.s_completions);
                CHECK_EQ(a.s_cycles_stalled_on_input, b.s_cycles_stalled_on_input);
                CHECK_EQ(a.s_cycles_stalled_on_buffer, b.s_cycles_stalled_on_buffer);
                CHECK(a.next_available_cycle == b.next_available_cycle);
                CHECK(a.preq_resource_count == b.preq_resource_count);
                CHECK_EQ(a.event_queue.size(), b.event_queue.size());
                for (size_t j = 0; j < std::min(a.event_queue.size(), b.event_queue.size()); ++j)
                {
                    CHECK_EQ(a.event_queue[j].prod_id, b.event_queue[j].prod_id);
                    CHECK_EQ(a.event_queue[j].count, b.event_queue[j].count);
                    CHECK_EQ(a.event_queue[j].ready_cycle, b.event_queue[j].ready_cycle);
                }
            }
            CHECK_EQ(stepped.consume(2), skipped.consume(2));
        }
    }
}

void test_ideal_production_timing_and_capacity()
{
    BulkProducer producer("ideal_epr", 1000.0);
    producer.add_level(ideal_epr_distribution(D), 1);
    CHECK_EQ(producer.top_level_buffer_capacity(), size_t{8});
    CHECK_EQ(producer.top_level_buffer_occupancy(), size_t{0});
    CHECK_EQ(producer.top_level_infidelity(), 0.0);
    CHECK_EQ(producer.physical_qubit_count(), size_t{0});
    for (size_t pairs = 1; pairs <= 8; pairs++)
    {
        advance_to({&producer}, pairs*PERIOD);
        CHECK_EQ(producer.top_level_buffer_occupancy(), pairs-1);
        producer.tick();
        CHECK_EQ(producer.top_level_buffer_occupancy(), pairs);
    }
    advance_to({&producer}, 12*PERIOD);
    CHECK_EQ(producer.top_level_buffer_occupancy(), size_t{8});
    CHECK(!producer.next_event_cycle().has_value());
    CHECK(producer.consume(2));
    producer.tick(); // Resume production after freeing buffer space.
    const auto ready = producer.next_event_cycle();
    CHECK(ready.has_value());
    if (ready)
    {
        CHECK_EQ(*ready, 13*PERIOD);
        advance_to({&producer}, *ready);
        CHECK_EQ(producer.top_level_buffer_occupancy(), size_t{6});
        producer.tick();
        CHECK_EQ(producer.top_level_buffer_occupancy(), size_t{7});
    }
}

void test_coupled_access_requires_two_pairs()
{
    BulkProducer producer("ideal_epr", 1000.0);
    producer.add_level(ideal_epr_distribution(D), 1);
    EPRMemory memory(1000.0, 12, 72, 12, D, &producer);
    Qubit ld{.qubit_id=0}, st{.qubit_id=1};
    std::vector<Qubit*> qubits{&ld};
    memory.striped_mapping(qubits.begin(), qubits.end());
    CHECK(!memory.do_coupled_load_store(&ld, &st).success());
    CHECK(memory.next_ready_cycle_for_load(&ld) > memory.current_cycle());
    advance_to({&producer, &memory}, PERIOD+1);
    CHECK(memory.has_epr_credit(&ld, Instruction::Type::LOAD));
    CHECK(!memory.has_epr_credit(&ld, Instruction::Type::COUPLED_LOAD_STORE));
    CHECK(memory.next_ready_cycle_for_access(&ld, Instruction::Type::LOAD) <= memory.current_cycle());
    CHECK(memory.next_ready_cycle_for_access(&ld, Instruction::Type::COUPLED_LOAD_STORE) > memory.current_cycle());
    CHECK(!memory.do_coupled_load_store(&ld, &st).success());
    CHECK_EQ(producer.top_level_buffer_occupancy(), size_t{1});
    CHECK_EQ(memory.blocks()[0].count(&ld), size_t{1});
    advance_to({&producer, &memory}, 2*PERIOD+1);
    const auto start = memory.current_cycle();
    CHECK(memory.next_ready_cycle_for_access(&ld, Instruction::Type::COUPLED_LOAD_STORE) <= start);
    auto result = memory.do_coupled_load_store(&ld, &st);
    CHECK(result.success());
    CHECK_EQ(result.latency, cycle_type{2*D});
    CHECK_EQ(producer.top_level_buffer_occupancy(), size_t{0});
    CHECK_EQ(memory.s_epr_consumed, uint64_t{2});
    CHECK_EQ(memory.blocks()[0].count(&ld), size_t{0});
    CHECK_EQ(memory.blocks()[0].count(&st), size_t{1});
    CHECK_EQ(memory.next_ready_cycle_for_load(&st), start+3*D);
    CHECK(memory.next_ready_cycle_for_access(&st, Instruction::Type::COUPLED_LOAD_STORE) >= start+3*D);
}

void test_load_store_credits_and_block_adapter()
{
    BulkProducer producer("ideal_epr", 1000.0);
    producer.add_level(ideal_epr_distribution(D), 1);
    EPRMemory memory(1000.0, 12, 72, 12, D, &producer);
    Qubit ld{.qubit_id=0}, st{.qubit_id=1};
    std::vector<Qubit*> qubits{&ld};
    memory.striped_mapping(qubits.begin(), qubits.end());
    advance_to({&producer, &memory}, 2*PERIOD+1);
    auto load = memory.do_load(&ld);
    CHECK(load.success());
    CHECK_EQ(load.latency, cycle_type{2*D});
    CHECK_EQ(producer.top_level_buffer_occupancy(), size_t{1});
    CHECK(!memory.do_store(&st).success());
    CHECK_EQ(producer.top_level_buffer_occupancy(), size_t{1});
    advance_to({&producer, &memory}, memory.current_cycle()+2*D);
    auto store = memory.do_store(&st);
    CHECK(store.success());
    CHECK_EQ(store.latency, cycle_type{0});
    CHECK_EQ(producer.top_level_buffer_occupancy(), size_t{0});
    CHECK_EQ(memory.s_epr_consumed, uint64_t{2});
}

void test_independent_blocks_have_no_memory_routing_contention()
{
    BulkProducer producer("ideal_epr", 1000.0);
    producer.add_level(ideal_epr_distribution(D), 1);
    EPRMemory memory(1000.0, 9*12, 72, 12, D, &producer);
    std::vector<Qubit> qubits(9);
    std::vector<Qubit*> ptrs;
    for (auto& q : qubits)
        ptrs.push_back(&q);
    memory.striped_mapping(ptrs.begin(), ptrs.end());
    advance_to({&producer, &memory}, 8*PERIOD+1);
    for (size_t i = 0; i < 8; i++)
        CHECK(memory.do_load(ptrs[i]).success());
    CHECK(!memory.do_load(ptrs[8]).success());
    CHECK_EQ(memory.s_epr_consumed, uint64_t{8});
    CHECK_EQ(memory.blocks()[8].count(ptrs[8]), size_t{1});
}

void test_compute_routing_still_blocks_memory_access()
{
    BulkProducer producer("ideal_epr", 1000.0);
    producer.add_level(ideal_epr_distribution(D), 1);
    EPRMemory memory(1000.0, 12, 72, 12, D, &producer);
    ComputeSubsystem compute(1000.0, D, 2, nullptr, {&memory});
    Qubit st{.qubit_id=0, .client_id=0}, other{.qubit_id=1, .client_id=0},
          ld{.qubit_id=2, .client_id=0};
    compute.initialize_qubits({&st, &other, &ld});
    advance_to({&producer, &memory, &compute}, 8*PERIOD+1);
    // Reserve the compute bus for two consecutive store intervals.
    CHECK(compute.rdr_simulate_store(&st));
    CHECK(compute.rdr_simulate_store(&st));
    Instruction access(Instruction::Type::COUPLED_LOAD_STORE, {2, 0});
    CHECK_EQ(compute.execute_instruction(&access, {&ld, &st}).progress, 0);
    CHECK_EQ(producer.top_level_buffer_occupancy(), size_t{8});
    CHECK_EQ(memory.s_epr_consumed, uint64_t{0});
    advance_to({&producer, &memory, &compute}, compute.current_cycle()+2*D);
    CHECK_EQ(compute.execute_instruction(&access, {&ld, &st}).progress, 1);
    CHECK_EQ(producer.top_level_buffer_occupancy(), size_t{6});
}

void test_ideal_distribution_has_no_distillation_adapter_delay()
{
    BulkProducer producer("ideal_epr", 1000.0);
    producer.add_level(ideal_epr_distribution(D), 1);
    EPRMemory memory(1000.0, 12, 72, 12, D, &producer);
    Qubit q{.qubit_id=0};
    advance_to({&producer, &memory}, 8*PERIOD+1);
    CHECK(memory.do_store(&q).success());
    const auto ready = memory.next_ready_cycle_for_load(&q);
    CHECK_EQ(ready, memory.current_cycle()+D);
    advance_to({&producer, &memory}, ready);
    CHECK(memory.do_load(&q).success());
    CHECK_EQ(producer.top_level_buffer_occupancy(), size_t{6});
}

void test_distillation_retains_restart_delay()
{
    BulkProducer producer("distilled_epr", 1000.0);
    producer.add_level(ed_protocol_2a(EDImplMethod::DISAGGREGATED_TYPE_A), 1);
    producer.tick();
    const auto first = producer.next_event_cycle();
    CHECK(first.has_value());
    if (!first)
        return;
    advance_to({&producer}, *first+1);
    CHECK_EQ(producer.top_level_buffer_occupancy(), size_t{2});
    CHECK(producer.consume(2));
    producer.tick();
    CHECK(producer.next_event_cycle().has_value());
    CHECK_EQ(*producer.next_event_cycle(), 2*(*first)+1);
}

void test_production_uses_memory_clock()
{
    BulkProducer producer("ideal_epr", 500.0);
    producer.add_level(ideal_epr_distribution(D), 1);
    BBMemory faster_clock(1000.0, 12, 72, 12, D);
    coordinate_clock_scale({&producer, &faster_clock});
    for (size_t i = 0; i < 2*PERIOD; i++)
    {
        producer.tick();
        faster_clock.tick();
    }
    CHECK_EQ(producer.current_cycle(), PERIOD);
    CHECK_EQ(producer.top_level_buffer_occupancy(), size_t{0});
    producer.tick();
    CHECK_EQ(producer.top_level_buffer_occupancy(), size_t{1});
}
} // namespace

int main()
{
    RUN(test_production_fast_forward_matches_ticks);
    RUN(test_ideal_production_timing_and_capacity);
    RUN(test_coupled_access_requires_two_pairs);
    RUN(test_load_store_credits_and_block_adapter);
    RUN(test_independent_blocks_have_no_memory_routing_contention);
    RUN(test_compute_routing_still_blocks_memory_access);
    RUN(test_ideal_distribution_has_no_distillation_adapter_delay);
    RUN(test_distillation_retains_restart_delay);
    RUN(test_production_uses_memory_clock);
    return qstest::finish();
}
