/*
    author: Suhas Vittal
    date:   26 August 2025
*/

#include "instruction.h"

#include <iostream>
#include <zlib.h>

int main(int argc, char* argv[])
{
    if (argc < 1)
    {
        INSTRUCTION::fpa_type angle = convert_float_to_fpa<INSTRUCTION::FPA_PRECISION>(0.4176);
        std::vector<INSTRUCTION::TYPE> urotseq{INSTRUCTION::TYPE::SDG, INSTRUCTION::TYPE::H};
        INSTRUCTION inst(INSTRUCTION::TYPE::RZ, {1}, angle, urotseq.begin(), urotseq.end());

        std::cout << inst << "\n";

        auto enc1 = inst.serialize();

        gzFile ostrm = gzopen("test.gz", "wb");
        enc1.read_write([&ostrm] (void* data, size_t size) { gzwrite(ostrm, data, size); });
        gzclose(ostrm);

        auto enc2 = INSTRUCTION::io_encoding{};
        gzFile istrm = gzopen("test.gz", "rb");
        enc2.read_write([&istrm] (void* data, size_t size) { gzread(istrm, data, size); });
        gzclose(istrm);

        INSTRUCTION inst2(std::move(enc2));
        std::cout << inst2 << "\n";
    }
    else
    {
        gzFile istrm = gzopen(argv[1], "rb");
        uint32_t num_qubits;
        gzread(istrm, &num_qubits, sizeof(num_qubits));
        std::cout << "num_qubits: " << num_qubits << "\n";
        while (!gzeof(istrm))
        {
            INSTRUCTION::io_encoding enc{};
            enc.read_write([&istrm] (void* data, size_t size) { gzread(istrm, data, size); });

            INSTRUCTION inst(std::move(enc));
            std::cout << inst << "\n";
        }
        gzclose(istrm);
    }

    return 0;
}