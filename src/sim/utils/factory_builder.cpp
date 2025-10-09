/*
 *  author: Suhas Vittal
 *  date:   8 October 2025
 * */

#include "sim/utils/estimation.h"
#include "sim/utils/factory_builder.h"

namespace sim
{
namespace util
{

////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////

std::vector<FACTORY_INFO> 
make_factory_config(double e)
{
    // assumes `PHYS_ERROR==1e-3`

    std::vector<FACTORY_INFO> factory_conf;
    factory_conf.reserve(2);

    if (e >= 1e-8)
    {
        factory_conf.push_back({"15to1", 17, 7, 7, 1e-8});
    }
    else if (e >= 1e-12)
    {
        factory_conf.push_back({"15to1", 11, 5, 5, 1e-6});
        factory_conf.push_back({"15to1", 25, 11, 11, 1e-12});
    }
    else if (e >= 1e-14)
    {
        factory_conf.push_back({"15to1", 13, 5, 5, 1e-7});
        factory_conf.push_back({"15to1", 29, 11, 13, 1e-14});
    }
    else
    {
        factory_conf.push_back({"15to1", 17, 7, 7, 1e-18});
        factory_conf.push_back({"15to1", 41, 17, 17, 1e-18});
    }

    return factory_conf;
}

////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////

T_FACTORY*
create_factory_from_info(const FACTORY_INFO& fi, double freq_khz, size_t level, size_t buffer_capacity)
{
    size_t initial_input_count;
    size_t output_count;
    size_t num_rotation_steps;

    if (fi.which == "15to1")
    {
        initial_input_count = 4;
        output_count = 1;
        num_rotation_steps = 11;
    }
    else if (fi.which == "20to4")
    {
        initial_input_count = 3;
        output_count = 4;
        num_rotation_steps = 17;
    }
    else
    {
        throw std::runtime_error("_create_factory: unknown factory type " + fi.which);
    }

    T_FACTORY* f = new T_FACTORY{freq_khz, 
                                    fi.e_out, 
                                    initial_input_count,
                                    output_count,
                                    num_rotation_steps,
                                    buffer_capacity,
                                    level};
    return f;
}

////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////

// we will make one L2 factory for every `L2_L1_RATIO` L1 factories:
constexpr size_t L2_L1_RATIO{8};

factory_build_result_type
factory_build(double target_error_rate,
                size_t max_phys_qubits,
                uint64_t l1_round_ns,
                uint64_t l2_round_ns,
                size_t pin_limit)
{
    std::vector<FACTORY_INFO> factory_conf = make_factory_config(target_error_rate);
    bool l2_factory_exists = (factory_conf.size() > 1);

    std::vector<T_FACTORY*> l1_fact, l2_fact;
    size_t qubit_count{0};

    while ((qubit_count < max_phys_qubits || l1_fact.empty() || (l2_factory_exists && l2_fact.empty()))
            && ((!l2_factory_exists && l1_fact.size() <= pin_limit) || (l2_factory_exists && l2_fact.size() <= pin_limit)))
    {
        // check if there is an L2 factory in our spec. If so, make one
        if (l2_factory_exists)
        {
            const FACTORY_INFO& l2_fact_conf = factory_conf[1];

            double freq_khz = sim::compute_freq_khz(l2_round_ns, l2_fact_conf.sc_dm);
            T_FACTORY* f = create_factory_from_info(l2_fact_conf, freq_khz, 1);
            l2_fact.push_back(f);
            
            size_t sc_q_count = est::sc_phys_qubit_count(l2_fact_conf.sc_dx, l2_fact_conf.sc_dz);
            qubit_count += sc_q_count * est::fact_logical_qubit_count(l2_fact_conf.which);
        }

        const FACTORY_INFO& l1_fact_conf = factory_conf[0];
        for (size_t i = 0; i < L2_L1_RATIO 
                            && (qubit_count < max_phys_qubits || l1_fact.empty()) 
                            && (l2_factory_exists || l1_fact.size() <= pin_limit); i++)
        {
            double freq_khz = sim::compute_freq_khz(l1_round_ns, l1_fact_conf.sc_dm);
            T_FACTORY* f = create_factory_from_info(l1_fact_conf, freq_khz, 0);
            l1_fact.push_back(f);

            size_t sc_q_count = est::sc_phys_qubit_count(l1_fact_conf.sc_dx, l1_fact_conf.sc_dz);
            qubit_count += sc_q_count * est::fact_logical_qubit_count(l1_fact_conf.which);
        }
    }

    if (l2_factory_exists)
    {
        if (l1_fact.empty())
            throw std::runtime_error("factory_build: no L1 factories found");

        for (auto* f : l1_fact)
            f->next_level_ = l2_fact;
        for (auto* f : l2_fact)
            f->previous_level_ = l1_fact;
    }

    std::vector<T_FACTORY*> factories(l1_fact.size() + l2_fact.size());
    std::move(l1_fact.begin(), l1_fact.end(), factories.begin());
    std::move(l2_fact.begin(), l2_fact.end(), factories.begin() + l1_fact.size());

    return {factories, qubit_count, factory_conf};
}

////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////

}   // namespace util
}   // namespace sim
