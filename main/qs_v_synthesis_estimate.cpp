/*
 *  author: Claude (Opus 5)
 *  date:   4 September 2026
 *
 *  Estimates the reduction in non-Clifford gate count if Rz/Rx synthesis
 *  targeted Clifford+V (V = sqrt(T)) instead of Clifford+T.
 *
 *  Empirically, T-count and V-count scale with the target precision as:
 *      T-count = 0.53 * log2(1/eps)
 *      V-count = 0.23 * log2(1/eps)
 *  (see e.g. Ross-Selinger-style synthesis cost curves.)
 *
 *  For each rotation already synthesized (Clifford+T) in a benchmark binary,
 *  its stored `urotseq` gives an actual T-count `n`. We invert the T curve to
 *  recover the implied precision, log2(1/eps) = n / 0.53, and use that to
 *  predict the V-count for the same rotation: v = ceil(0.23 * n / 0.53).
 *
 *  NOTE: both curves are modeled as linear through the origin (no additive
 *  constant term), so this per-rotation ratio reduces to a fixed
 *  0.23/0.53 ≈ 0.434 regardless of precision. The only thing that varies
 *  benchmark-to-benchmark is what fraction of its non-Clifford gates come
 *  from rotations (which benefit) versus explicit T-gates and Toffolis
 *  (which don't -- they carry no synthesis error budget, so V-synthesis
 *  cannot reduce them, and they are counted 1:1 in both totals below).
 * */

#include "argparse/argparse.h"
#include "generic_io.h"
#include "globals.h"
#include "instruction.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

constexpr double T_SYNTH_COEFF{0.53};   // T-count = 0.53 * log2(1/eps)
constexpr double V_SYNTH_COEFF{0.23};   // V-count = 0.23 * log2(1/eps)

// From CCZ_UOPS table in src/instruction.cpp: CCX/CCZ each expand to
// 7 T-like gates (4xT + 3xTDG) and 6xCX.
constexpr uint64_t TOFFOLI_T_GATE_COUNT{7};

// benchmark name = input file basename with a trailing .xz/.gz suffix stripped.
std::string
benchmark_name_from_path(const std::string& path)
{
    std::string name = std::filesystem::path(path).filename().string();
    for (std::string_view suffix : {".xz", ".gz"})
    {
        if (name.size() > suffix.size() && name.compare(name.size()-suffix.size(), suffix.size(), suffix) == 0)
        {
            name.resize(name.size()-suffix.size());
            break;
        }
    }
    return name;
}

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main(int argc, char* argv[])
{
    std::string input_file;
    int64_t     print_progress;
    std::string csv_file;

    ARGPARSE()
        .required("input-file", "Binary trace file to analyze", input_file)
        .optional("-rdr", "", "Use RDR ISA", GL_USE_RDR_ISA, 0)
        .optional("-pp", "--print-progress", "Progress print-out frequency", print_progress, 1'000'000)
        .optional("-csv", "", "Append a summary row to this CSV file", csv_file, std::string{})
        .parse(argc, argv);

    generic_strm_type istrm;
    generic_strm_open(istrm, input_file, "rb");

    uint32_t num_qubits_raw;
    generic_strm_read(istrm, &num_qubits_raw, sizeof(num_qubits_raw));
    size_t num_qubits = static_cast<size_t>(num_qubits_raw);

    uint64_t num_rotations{0};
    uint64_t t_explicit{0};
    uint64_t t_from_ccx{0};
    uint64_t t_from_rz{0};
    uint64_t v_from_rz{0};

    uint64_t inst_count{0};

    while (!generic_strm_eof(istrm))
    {
        if (inst_count % print_progress == 0)
            std::cerr << "progress: " << inst_count << " instructions read\n";

        Instruction* inst = read_instruction_from_stream(istrm);
        if (generic_strm_eof(istrm))
        {
            delete inst;
            break;
        }

        inst_count++;
        if (is_software_instruction(inst->type))
        {
            delete inst;
            continue;
        }

        if (is_t_like_instruction(inst->type))
        {
            t_explicit++;
        }
        else if (is_toffoli_like_instruction(inst->type))
        {
            t_from_ccx += TOFFOLI_T_GATE_COUNT;
        }
        else if (is_rotation_instruction(inst->type))
        {
            const uint64_t n = std::count_if(inst->urotseq.begin(), inst->urotseq.end(),
                                            [] (auto t) { return is_t_like_instruction(t); });
            t_from_rz += n;
            v_from_rz += static_cast<uint64_t>(std::ceil(V_SYNTH_COEFF * static_cast<double>(n) / T_SYNTH_COEFF));
            num_rotations++;
        }
        // everything else (H, S-like, CX-like, memory, measurement) is Clifford
        // or otherwise unaffected by rotation synthesis -- ignore it here.

        delete inst;
    }

    std::cerr << "DONE\n";

    generic_strm_close(istrm);

    const uint64_t baseline_nonclifford = t_explicit + t_from_ccx + t_from_rz;
    const uint64_t v_model_nonclifford  = t_explicit + t_from_ccx + v_from_rz;
    const uint64_t abs_reduction        = baseline_nonclifford - v_model_nonclifford;

    const double pct_reduction    = (baseline_nonclifford > 0) ? 100.0*fpdiv(abs_reduction, baseline_nonclifford) : 0.0;
    const double rz_pct_reduction = (t_from_rz > 0) ? 100.0*fpdiv(t_from_rz-v_from_rz, t_from_rz) : 0.0;
    const double rz_share         = (baseline_nonclifford > 0) ? 100.0*fpdiv(t_from_rz, baseline_nonclifford) : 0.0;

    print_stat_line(std::cout, "NUM_QUBITS",              num_qubits);
    print_stat_line(std::cout, "NUM_ROTATIONS",           num_rotations);
    print_stat_line(std::cout, "T_EXPLICIT",              t_explicit);
    print_stat_line(std::cout, "T_FROM_CCX",              t_from_ccx);
    print_stat_line(std::cout, "T_FROM_RZ",                t_from_rz);
    print_stat_line(std::cout, "V_FROM_RZ",                v_from_rz);
    print_stat_line(std::cout, "BASELINE_NONCLIFFORD",     baseline_nonclifford);
    print_stat_line(std::cout, "V_MODEL_NONCLIFFORD",      v_model_nonclifford);
    print_stat_line(std::cout, "ABS_REDUCTION",            abs_reduction);
    print_stat_line(std::cout, "PCT_REDUCTION",            pct_reduction);
    print_stat_line(std::cout, "RZ_PCT_REDUCTION",         rz_pct_reduction);
    print_stat_line(std::cout, "RZ_SHARE_OF_NONCLIFFORD_%", rz_share);

    if (!csv_file.empty())
    {
        const bool file_exists = std::filesystem::exists(csv_file);
        std::ofstream csv(csv_file, std::ios::app);
        if (!file_exists)
        {
            csv << "benchmark,num_qubits,num_rotations,t_explicit,t_from_ccx,t_from_rz,v_from_rz,"
                   "baseline_nonclifford,v_model_nonclifford,abs_reduction,pct_reduction\n";
        }
        csv << benchmark_name_from_path(input_file) << ','
            << num_qubits << ','
            << num_rotations << ','
            << t_explicit << ','
            << t_from_ccx << ','
            << t_from_rz << ','
            << v_from_rz << ','
            << baseline_nonclifford << ','
            << v_model_nonclifford << ','
            << abs_reduction << ','
            << pct_reduction << '\n';
    }

    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
