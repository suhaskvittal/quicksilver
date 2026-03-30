/*
 *  author: Suhas Vittal
 *  date:   29 March 2026
 * */

#ifndef BISQUIT_CIRCUIT_UTILS_h
#define BISQUIT_CIRCUIT_UTILS_h

#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * `GATE` is a description for a gate. Initialize this
 * using the builder formalism.
 *
 * Example usage:
 *      `ostrm << GATE("h").operand("q");`
 * Writes the QASM for an H gate on "q" to `ostrm`.
 *
 * For rotation gates:
 *      ```
 *      ostrm << GATE("rz")
 *                  .arg(0.892)
 *                  .operand("qr", {0});
 *      ```
 * Writes "rz(0.892) qr[0];" to `ostrm`.
 *
 * Multi-qubit gates:
 *      ```
 *      ostrm << GATE("ccx").operand("qr", {0, 1, 4});
 *      ```
 * Writes "ccx qr[0], qr[1], qr[4];".
 * */

struct GATE
{
    std::string_view name;
    std::vector<std::string> args;
    std::vector<std::string> operands;

    GATE(std::string_view);

    GATE& arg(int);
    GATE& arg(double);
    GATE& arg(std::string_view);

    GATE& operand(std::string_view);
    GATE& operand(std::string_view, std::vector<int>);

    std::string to_string() const;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::ostream& operator<<(std::ostream&, const GATE&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif // BISQUIT_CIRCUIT_UTILS_h
