/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#include <deque>

namespace compile
{
namespace memory_scheduler
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class SCHEDULER_IMPL> stats_type
run(generic_strm_type& ostrm, generic_strm_type& istrm, const SCHEDULER_IMPL& scheduler, config_type conf)
{
    constexpr size_t OUTGOING_CAPACITY{16384};

    stats_type stats;

    // read number of qubits from `istrm`
    uint32_t num_qubits;
    generic_strm_read(istrm, &num_qubits, sizeof(num_qubits));
    generic_strm_write(ostrm, &num_qubits, sizeof(num_qubits));

    // initialize `active_set`:
    active_set_type active_set;
    active_set.reserve(conf.active_set_capacity);
    for (qubit_type i = 0; i < conf.active_set_capacity; i++)
        active_set.insert(i);

    dag_ptr               dag{new DAG{num_qubits}};
    std::deque<inst_ptr>  outgoing_buffer;
    int64_t               inst_done{0};
    while (inst_done < conf.inst_compile_limit && !generic_strm_eof(istrm))
    {
        const uint64_t inst_done_before{inst_done};

        // try to fill up the DAG during every iteration
        if (!generic_strm_eof(istrm))
            read_instructions_into_dag(dag, istrm, conf.dag_inst_capacity);

        // try to complete as many instructions as possible using the `active_set`
        auto completable = dag->get_front_layer_if(
                                [&active_set] (auto* inst) { return instruction_is_ready(inst, active_set); });
        if (completable.empty())
        {
            // scheduling epoch: invoke the memory access scheduler:
            auto out = scheduler(active_set, dag, conf);
            assert(out.active_set.size() == conf.active_set_capacity);

            outgoing_buffer.insert(outgoing_buffer.end(), out.memory_accesses.begin(), out.memory_accesses.end());
            active_set = std::move(out.active_set);

            stats.memory_accesses += out.memory_accesses.size();
            stats.total_unused_bandwidth += out.unused_bandwidth;
            stats.scheduler_epochs++;
        }
        else
        {
            for (auto* inst : completable)
            {
                outgoing_buffer.push_back(inst);
                dag->remove_instruction_from_front_layer(inst);
                inst_done += inst->uop_count();
            }
        }

        if (outgoing_buffer.size() >= OUTGOING_CAPACITY)
        {
            auto begin = outgoing_buffer.begin();
            auto end = begin + (OUTGOING_CAPACITY/2);
            drain_buffer_into_stream(begin, end, ostrm);
            outgoing_buffer.erase(begin, end);
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

    drain_buffer_into_stream(outgoing_buffer.begin(), outgoing_buffer.end(), ostrm);
    outgoing_buffer.clear();

    stats.unrolled_inst_done = inst_done;
    return stats;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class ITER> void
drain_buffer_into_stream(ITER begin, ITER end, generic_strm_type& ostrm)
{
    std::for_each(begin, end, 
            [&ostrm] (inst_ptr inst) 
            {
                write_instruction_to_stream(ostrm, inst);
                delete inst;
            });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace memory_scheduler
}  // namespace compile
