/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#include <deque>

namespace compiler
{
namespace pass
{
namespace memory_scheduler
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class SchedulerImpl> Stats
run(generic_strm_type& ostrm, generic_strm_type& istrm, const SchedulerImpl& scheduler, Config conf)
{
    Stats stats;

    // read number of qubits from `istrm`
    IOUtility io(istrm, ostrm);

    // initialize `active_set`:
    active_set_type active_set;
    active_set.reserve(conf.active_set_capacity);
    for (qubit_type i = 0; i < conf.active_set_capacity; i++)
        active_set.insert(i);

    dag_ptr  dag{new DAG{io.num_qubits}};
    int64_t  inst_done{0};
    while (inst_done < conf.inst_compile_limit && (dag->inst_count() > 0 || !generic_strm_eof(istrm)))
    {
        const uint64_t inst_done_before{inst_done};

        // try to fill up the DAG during every iteration
        io.read_instructions(dag.get(), conf.dag_inst_capacity);

        // try to complete as many instructions as possible using the `active_set`
        auto completable = dag->get_front_layer_if(
                                [&active_set] (auto* inst) { return instruction_is_ready(inst, active_set); });
        if (completable.empty())
        {
            // scheduling epoch: invoke the memory access scheduler:
            auto out = scheduler(active_set, dag, conf);
            assert(out.active_set.size() == conf.active_set_capacity);

            io.write_instructions(out.memory_accesses.begin(), out.memory_accesses.end());
            active_set = std::move(out.active_set);

            stats.memory_accesses += out.memory_accesses.size();
            stats.total_unused_bandwidth += out.unused_bandwidth;
            stats.scheduler_epochs++;
        }
        else
        {
            for (auto* inst : completable)
            {
                io.write_instruction(inst);
                dag->remove_instruction_from_front_layer(inst);
                inst_done += inst->uop_count();
            }
        }

        if (conf.print_progress_frequency > 0
            && (inst_done % conf.print_progress_frequency) < (inst_done_before % conf.print_progress_frequency))
        {
            auto front_layer = dag->get_front_layer();

            std::cout << "\nMemory Scheduler ============================================="
                        << "\ninstructions done = " << inst_done
                        << "\nmemory accesses   = " << stats.memory_accesses
                        << "\nscheduling epochs = " << stats.scheduler_epochs
                        << "\nactive set =";
            for (auto q : active_set)
                std::cout << " " << q;
            std::cout << "\nDAG inst count = " << dag->inst_count()
                            << " of " << conf.dag_inst_capacity
                            << ", front layer =";
            if (front_layer.size() > 8)
            {
                std::cout << " (hidden, width = " << front_layer.size() << ")";
            }
            else
            {
                for (auto* inst : front_layer)
                    std::cout << "\n\t" << *inst;
            }
            std::cout << "\n";
        }
    }

    stats.unrolled_inst_done = inst_done;
    return stats;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace memory_scheduler
}  // namespace pass
}  // namespace compiler
