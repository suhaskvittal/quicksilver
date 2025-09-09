/*
    author: Suhas Vittal
    date:   20 September 2025
*/

#ifndef BENCHMARK_SHOR_h
#define BENCHMARK_SHOR_h

#include "fixed_point/numeric.h"
#include "instruction.h"

#include <string>
#include <string_view>

#include <zlib.h>

namespace benchmark
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct SHOR
{
public:
    using integral_type = BIGINT_TYPE<4096>;
    using qubit_register_range_type = std::pair<qubit_type, qubit_type>;
private:
    integral_type public_key_{};

    gzFile ostrm_{};
public:
    ~SHOR();

    void build_program_and_write_to_file(std::string output_file, std::string_view public_key_hex_string);
private:
    SHOR(std::string output_file, integral_type&& pub);

    void fourier_adder(qubit_register_range_type, integral_type a);  // adds `a` to the state of the qubits in the register
    void mod_adder(qubit_type c1, qubit_type c2, qubit_register_range_type, qubit_type anc, integral_type a);
    void cmul(qubit_type c, qubit_register_range_type x, qubit_register_range_type b, integral_type a);
    void cunitary(qubit_type c, qubit_register_range_type x, qubit_register_range_type anc, integral_type a, integral_type a_inv);
};
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace benchmark


#endif  // BENCHMARK_SHOR_h