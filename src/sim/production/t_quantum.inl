/*
 *  author: Suhas Vittal
 *  date:   7 September 2026
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
    size_t tries_until_success = 1.0/prob_success;
    cycle_type t = ((escape_cycles/2) * (tries_until_success-1)) + escape_cycles + GL_REACTION_TIME;

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
    // add memory errors atop errors due to T infidelity
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
