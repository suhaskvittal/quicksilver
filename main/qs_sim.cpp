/*
    author: Suhas Vittal
    date:   26 August 2025
*/

#include "sim.h"

template <class T> void
print_stat_line(std::string_view name, T value)
{
    std::cout << std::setw(48) << std::left << name << " : " << std::setw(12) << std::right << value << "\n";
}

int 
main(int argc, char** argv)
{
    // parse input arguments;
    std::string trace_file{argv[1]};

    SIM::CONFIG cfg;
    cfg.client_trace_files.push_back(trace_file);

    std::cout << "config setup done\n";

    SIM sim(cfg);

    std::vector<uint64_t> last_printed_inst_count(sim.clients().size(), 0);
    while (!sim.is_done())
    {
        sim.tick();

        // check if any client has reached modulo 100K instructions
        const auto& clients = sim.clients();
        for (size_t i = 0; i < clients.size(); i++)
        {
            const auto& c = clients[i];
            if (c->s_inst_done % 100'000 == 0 && c->s_inst_done != last_printed_inst_count[i])
            {
                std::cout << "( cycle = " << GL_CYCLE << " ) client " << i << " : " << c->s_inst_done << " instructions\n";
                last_printed_inst_count[i] = c->s_inst_done;
            }
        }
    }

    // print stats for each client:
    const auto& clients = sim.clients();
    for (size_t i = 0; i < clients.size(); i++)
    {
        const auto& c = clients[i];
        std::cout << "CLIENT " << std::setw(4) << std::right << i << "\n";

        print_stat_line("VIRTUAL_INST_DONE", c->s_inst_done);
        print_stat_line("UNROLLED_INST_DONE", c->s_unrolled_inst_done);
        print_stat_line("CYCLES_STALLED", c->s_cycles_stalled);
        print_stat_line("CYCLES_STALLED_BY_MEM", c->s_cycles_stalled_by_mem);
        print_stat_line("CYCLES_STALLED_BY_ROUTING", c->s_cycles_stalled_by_routing);
        print_stat_line("CYCLES_STALLED_BY_RESOURCE", c->s_cycles_stalled_by_resource);
    }

    return 0;
}