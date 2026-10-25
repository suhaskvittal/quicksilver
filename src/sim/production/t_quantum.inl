/*
 *  author: Suhas Vittal
 *  date:   7 September 2026
 *  contributor: OpenAI Codex (compact 15-to-1 factory variant, 1 October 2026)
 * */

ProducerQuantum
t_cc_cultivation(size_t d)
{
    size_t escape_dist,
           escape_cycles;
    double prob_success,
           infidelity;

    if (d == 3)
    {
        escape_dist = 13;
        escape_cycles = 8;
        prob_success = 0.2;
        infidelity = 1e-6;
    }
    else if (d == 5)
    {
        escape_dist = 15;
        escape_cycles = 15;
        prob_success = 0.02;
        infidelity = 1e-8;
    }
    else
    {
        std::cerr << "t_cc_cultivation: unsupported cultivation distance: " << d << _die{};
    }

    // estimate time to succesful cultivation. notes:
    //  1. Assume error hapeens in the middle of `escape_cycles`
    size_t tries_until_success = static_cast<size_t>(std::round( 1.0/prob_success));
    //
    // assume that 50% of the failure occurs because of a nontrivial syndrome, and suppose that
    // the other 50% are because the syndrome's complementary gap is too low
    //
    //  1. overhead in first case is, on average, `escape_cycles/2`
    //  2. overhead in second case is always `escape_cycles+GL_REACTION_TIME`
    //
    cycle_type mean_fail_latency = escape_cycles/4 + (escape_cycles+GL_REACTION_TIME)/2;
    cycle_type latency = escape_cycles + GL_REACTION_TIME;
    cycle_type t = mean_fail_latency*(tries_until_success-1) + latency;

    return ProducerQuantum
            {
                .resource = ResourceType::T,
                .is_distiller = false,
                .buffer_size = 1,
                .infidelity = infidelity,
                .physical_qubit_count = configuration::surface_code_physical_qubit_count(escape_dist),
                .cycles_per_production = t
            };
}

ProducerQuantum
t_15to1_distillation(size_t dx, size_t dz, size_t dm, double e)
{
    size_t physical_qubit_count = 2*(dx + 4*dz)*3*dx + 4*dm;
    double memory_error_per_cycle = configuration::surface_code_logical_error_rate(dx, GL_PHYSICAL_ERROR_RATE);
    double infidelity = 35*e*e*e + 5*6*dm*memory_error_per_cycle;

    return ProducerQuantum
            {
                .resource = ResourceType::T,
                .is_distiller = true,
                .d_input_states = 15,
                .d_output_states = 1,
                .infidelity = infidelity,
                .physical_qubit_count = physical_qubit_count,
                .cycles_per_production = retry_adjusted_latency(6*dm, 15, e)
            };
}

ProducerQuantum
t_15to1_distillation_compact(size_t dx, size_t dz, size_t dm, double e)
{
    size_t physical_qubit_count = 4*dx*(dx+3*dz);
    cycle_type latency = 12*dm;
    double memory_error_per_cycle = configuration::surface_code_logical_error_rate(dx, GL_PHYSICAL_ERROR_RATE);
    double infidelity = 35*e*e*e + 5*latency*memory_error_per_cycle;

    return ProducerQuantum
                {
                    .resource = ResourceType::T,
                    .is_distiller = true,
                    .d_input_states = 15,
                    .d_output_states = 1,
                    .infidelity = infidelity,
                    .physical_qubit_count = physical_qubit_count,
                    .cycles_per_production = retry_adjusted_latency(latency, 15, e)
                };
}
