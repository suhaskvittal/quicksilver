/*
 *  author: Suhas Vittal
 *  date:   8 October 2025
 * */

#include "sim/utils/estimation.h"
#include "sim/utils/factory_builder.h"

namespace sim
{

bool GL_PREF_CULTIVATION{false};

namespace util
{

////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////

std::vector<FACTORY_INFO> 
make_factory_config(double e)
{
    // assumes `PHYS_ERROR==1e-3`

    const FACTORY_INFO C3_INFO = FACTORY_INFO::cultivation("c3", 1e-6, 0.20, 3, 18);
    const FACTORY_INFO C5_INFO = FACTORY_INFO::cultivation("c5", 1e-8, 0.02, 5, 25);

    std::vector<FACTORY_INFO> factory_conf;
    factory_conf.reserve(2);

    if (e >= 1e-8)
    {
        if (GL_PREF_CULTIVATION)
            factory_conf.push_back(C5_INFO);
        else
            factory_conf.push_back(FACTORY_INFO::distillation("15to1", 1e-8, 17, 7, 7));
    }
    else if (e >= 1e-12)
    {
        if (GL_PREF_CULTIVATION)
            factory_conf.push_back(C3_INFO);
        else
            factory_conf.push_back(FACTORY_INFO::distillation("15to1", 1e-6, 11, 5, 5));
        factory_conf.push_back(FACTORY_INFO::distillation("15to1", 1e-12, 25, 11, 11));
    }
    else if (e >= 1e-14)
    {
        if (GL_PREF_CULTIVATION)
            factory_conf.push_back(C5_INFO);
        else
            factory_conf.push_back(FACTORY_INFO::distillation("15to1", 1e-7, 13, 5, 5));
        factory_conf.push_back(FACTORY_INFO::distillation("15to1", 1e-14, 29, 11, 13));
    }
    else
    {
        factory_conf.push_back(FACTORY_INFO::distillation("15to1", 1e-8, 17, 7, 7));
        factory_conf.push_back(FACTORY_INFO::distillation("15to1", 1e-18, 41, 17, 17));
    }

    return factory_conf;
}

////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////

T_FACTORY*
create_factory_from_info(const FACTORY_INFO& fi, uint64_t round_ns, size_t level, size_t buffer_capacity)
{
    T_FACTORY* f;
    if (fi.is_cultivation())
    {
        double freq_khz = sim::compute_freq_khz(round_ns, fi.num_rounds);
        f = new T_CULTIVATION{freq_khz, fi.e_out, fi.probability_of_success, buffer_capacity, level};
    }
    else
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
            throw std::runtime_error("_create_factory: unknown distillation type " + fi.which);
        }

        double freq_khz = sim::compute_freq_khz(round_ns, fi.sc_dm);
        f = new T_DISTILLATION{freq_khz, fi.e_out, initial_input_count, output_count, num_rotation_steps, buffer_capacity, level};
    }

    return f;
}

////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////

size_t
get_factory_qubit_count(const FACTORY_INFO& fi)
{
    if (fi.is_cultivation())
    {
        size_t grafted_distance = fi.which == "c3" ? 9 : 15;
        return est::sc_phys_qubit_count(grafted_distance);
    }
    else
    {
        size_t sc_q_count = est::sc_phys_qubit_count(fi.sc_dx, fi.sc_dz);
        return sc_q_count * est::fact_logical_qubit_count(fi.which);
    }
}

////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////

// we will make one L2 factory for every `L2_L1_RATIO` L1 factories:
constexpr size_t L2_L1_RATIO_IF_D3_CULTIVATION{8};
constexpr size_t L2_L1_RATIO_IF_D5_CULTIVATION{64};
constexpr size_t L2_L1_RATIO_IF_DISTILLATION{8};

factory_build_result_type
factory_build(double target_error_rate,
                size_t max_phys_qubits,
                uint64_t l1_round_ns,
                uint64_t l2_round_ns,
                size_t pin_limit)
{
    std::vector<FACTORY_INFO> factory_conf = make_factory_config(target_error_rate);
    bool l2_factory_exists = (factory_conf.size() > 1);

    const FACTORY_INFO& l1_fact_conf = factory_conf[0];
    size_t l2_l1_ratio;
    if (l1_fact_conf.is_cultivation())
        l2_l1_ratio = l1_fact_conf.which == "c3" ? L2_L1_RATIO_IF_D3_CULTIVATION : L2_L1_RATIO_IF_D5_CULTIVATION;
    else
        l2_l1_ratio = L2_L1_RATIO_IF_DISTILLATION;

    std::vector<T_FACTORY*> l1_fact, l2_fact;
    size_t qubit_count{0};

    while ((qubit_count < max_phys_qubits || l1_fact.empty() || (l2_factory_exists && l2_fact.empty()))
            && ((!l2_factory_exists && l1_fact.size() <= pin_limit) || (l2_factory_exists && l2_fact.size() <= pin_limit)))
    {
        // check if there is an L2 factory in our spec. If so, make one
        if (l2_factory_exists)
        {
            const FACTORY_INFO& l2_fact_conf = factory_conf[1];

            T_FACTORY* f = create_factory_from_info(l2_fact_conf, l2_round_ns, 1);
            l2_fact.push_back(f);

            qubit_count += get_factory_qubit_count(l2_fact_conf);
        }

        for (size_t i = 0; i < l2_l1_ratio 
                            && (qubit_count < max_phys_qubits || l1_fact.empty()) 
                            && (l2_factory_exists || l1_fact.size() <= pin_limit); i++)
        {
            T_FACTORY* f = create_factory_from_info(l1_fact_conf, l1_round_ns, 0);
            l1_fact.push_back(f);

            qubit_count += get_factory_qubit_count(l1_fact_conf);
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
