/*
 *  author: Suhas Vittal
 *  date:   7 September 2026
 * */

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

size_t _ed_physical_qubit_count(EDImplMethod, 
                                size_t input_count, 
                                size_t pauli_distance,
                                size_t meas_distance);
cycle_type _ed_latency(EDImplMethod, 
                        size_t input_count, 
                        size_t check_count, 
                        size_t pauli_distance, 
                        size_t meas_distance);

/*
 * Additional code distance required in disaggregated setting on compute side as
 * compute must wait for memory to finish.
 * */
constexpr size_t D_EXT_CMP{4};

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ProducerQuantum
ideal_epr_distribution(size_t memory_code_distance)
{
    return ProducerQuantum{
        .resource = ResourceType::EPR,
        .is_distiller = false,
        .d_input_states = 0,
        .d_output_states = 1,
        .buffer_size = 1000,
        .infidelity = 0.0,
        .physical_qubit_count = 0,
        .cycles_per_production = 4 * memory_code_distance,
        .restart_delay = 0,
    };
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ProducerQuantum
ed_protocol_1a(EDImplMethod m)
{
    ProducerQuantum q
    {
        .resource = ResourceType::EPR,
        .is_distiller = true,
        .d_input_states = 14,
        .d_output_states = 6,
        .buffer_size = 6,
        .infidelity = 4.4e-6
    };

    // remaining variables in `q` depend on `m`
    size_t dp = 13,
           dm = 13;
    if (m == EDImplMethod::YOKED_SURFACE_CODE)
    {
        dp = 9;
    }
    else if (m == EDImplMethod::BIVARIATE_BICYCLE_CODE || m == EDImplMethod::DISAGGREGATED_TYPE_A)
    {
        dp = 12;
        dm = 12;
    }

    q.physical_qubit_count = _ed_physical_qubit_count(m, 14, dp, dm);
    q.cycles_per_production = retry_adjusted_latency(_ed_latency(m, 14, 8, dp, dm), 14, 0.01);
    return q;
}

ProducerQuantum
ed_protocol_1b(EDImplMethod m)
{
    ProducerQuantum q
    {
        .resource = ResourceType::EPR,
        .is_distiller = true,
        .d_input_states = 32,
        .d_output_states = 25,
        .buffer_size = 25,
        .infidelity = 6.1e-13
    };

    // remaining variables in `q` depend on `m`
    size_t dp = 25,
           dm = 25;
    if (m == EDImplMethod::YOKED_SURFACE_CODE)
    {
        dp = 15;
    }
    else if (m == EDImplMethod::BIVARIATE_BICYCLE_CODE || m == EDImplMethod::DISAGGREGATED_TYPE_A)
    {
        dp = 24;
        dm = 24;
    }

    q.physical_qubit_count = _ed_physical_qubit_count(m, 32, dp, dm);
    q.cycles_per_production = retry_adjusted_latency(_ed_latency(m, 32, 7, dp, dm), 32, 4.4e-6);
    return q;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ProducerQuantum
ed_protocol_2a(EDImplMethod m)
{
    ProducerQuantum q
    {
        .resource = ResourceType::EPR,
        .is_distiller = true,
        .d_input_states = 4,
        .d_output_states = 2,
        .buffer_size = 2,
        .infidelity = 4.9e-4
    };

    // remaining variables in `q` depend on `m`
    size_t dp = 9,
           dm = 9;
    if (m == EDImplMethod::YOKED_SURFACE_CODE)
    {
        dp = 5;
    }
    else if (m == EDImplMethod::BIVARIATE_BICYCLE_CODE || m == EDImplMethod::DISAGGREGATED_TYPE_A)
    {
        dp = 12;
        dm = 12;
    }

    q.physical_qubit_count = _ed_physical_qubit_count(m, 4, dp, dm);
    q.cycles_per_production = retry_adjusted_latency(_ed_latency(m, 4, 2, dp, dm), 4, 0.01);
    return q;
}

ProducerQuantum
ed_protocol_2b(EDImplMethod m)
{
    ProducerQuantum q
    {
        .resource = ResourceType::EPR,
        .is_distiller = true,
        .d_input_states = 27,
        .d_output_states = 18,
        .buffer_size = 18,
        .infidelity = 4.3e-10
    };

    // remaining variables in `q` depend on `m`
    size_t dp = 17,
           dm = 17;
    if (m == EDImplMethod::YOKED_SURFACE_CODE)
    {
        dp = 11;
    }
    else if (m == EDImplMethod::BIVARIATE_BICYCLE_CODE || m == EDImplMethod::DISAGGREGATED_TYPE_A)
    {
        dp = 18;
        dm = 18;
    }

    q.physical_qubit_count = _ed_physical_qubit_count(m, 27, dp, dm);
    q.cycles_per_production = retry_adjusted_latency(_ed_latency(m, 28, 9, dp, dm), 27, 4.9e-4);
    return q;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

size_t
_ed_physical_qubit_count(EDImplMethod m, size_t n, size_t dp, size_t dm)
{
    size_t dis_ysc_dp = std::max(size_t{5}, (dp+D_EXT_CMP+1)/2 + 1);
    if (dis_ysc_dp % 2 == 0)
        dis_ysc_dp++;

    switch (m)
    {
    using enum EDImplMethod;

    case BARE:
        // multiply by two as both systems need to support ED.
        return 2 * (3*n/2) * configuration::surface_code_physical_qubit_count(dp);
    case YOKED_SURFACE_CODE:
        return 2 * (3*(n+2)/2) * configuration::surface_code_physical_qubit_count(dp);
    case BIVARIATE_BICYCLE_CODE:
        return 2 * ((n+11)/12) * configuration::bivariate_bicycle_code_physical_qubit_count(dp);
    case DISAGGREGATED_TYPE_A:
        return (3*(n+2)/2) * configuration::surface_code_physical_qubit_count(dis_ysc_dp)
                + ((n+11)/12) * configuration::bivariate_bicycle_code_physical_qubit_count(dp);
    default:
        std::cerr << "_ed_physical_qubit_count: unsupported method" << _die{}; 
    }
}

cycle_type
_ed_latency(EDImplMethod m, size_t n, size_t c, size_t dp, size_t dm)
{
    switch (m)
    {
    using enum EDImplMethod;

    case BARE:
        return c*dm;
    case YOKED_SURFACE_CODE:
        // load overhead is d per input
        return n*dm + c*dm;
    case BIVARIATE_BICYCLE_CODE:
        return c*dm;  // assume inplace state preparation
    case DISAGGREGATED_TYPE_A:
        return c*dm;
    default:
        std::cerr << "_ed_physical_qubit_count: unsupported method" << _die{}; 
    }
}

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
