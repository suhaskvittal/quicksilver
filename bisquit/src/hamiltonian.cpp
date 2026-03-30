/*
 *  author: Suhas Vittal
 *  date:   29 March 2026
 * */

#include "circuit_utils.h"
#include "hamiltonian.h"

#include <H5Cpp.h>

#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>

namespace ham
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

/*
 * Writes a term for trotterization subroutine.
 *  `term` should be something like "x1 z2 y3" (space-separated lowercase tokens)
 *  `coeff` is the coefficient of the Pauli term
 * */

void _trotterization_write_term(std::ostream&,
                                std::string_view term,
                                double coeff,
                                /*
                                 * Registers:
                                 * */
                                std::string_view control_qubit,
                                std::string_view main_register);

/*
 * Iterates over each (pauli, qubit_index) pair in a term string.
 *  `term` is a space-separated string of lowercase tokens, e.g. "x0 z7 y15"
 *  The callback receives (char pauli, int qubit_index).
 * */
template <class CALLBACK>
void _for_each_in_pauli_term(std::string_view term, const CALLBACK& cb);

/*
 * Opens the HDF5 file at `filename` and iterates over all Pauli terms
 * stored in the text dataset at `key`.
 *  The callback receives (std::string term, double coeff) where
 *  `term` is in the space-separated lowercase format understood by
 *  `_for_each_in_pauli_term`.
 * */
template <class CALLBACK>
void _for_each_term_in_file(std::string_view filename,
                             std::string_view key,
                             const CALLBACK& cb);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
build_trotterization(std::string output_file,
                        std::string input_file,
                        double c_sum,
                        int trotter_steps,
                        bool qpe)
{
    std::ofstream ostrm(output_file);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class CALLBACK> void
_for_each_in_pauli_term(std::string_view term, const CALLBACK& cb)
{
    // `term` is space-separated lowercase tokens, e.g. "x0 z7 y15".
    // Each token: first char is the Pauli operator, remainder is the qubit index.
    size_t i = 0;
    while (i < term.size())
    {
        // skip whitespace
        while (i < term.size() && term[i] == ' ')
            ++i;
        if (i >= term.size())
            break;

        char p = term[i++];
        size_t j = i;
        while (j < term.size() && term[j] != ' ')
            ++j;

        int idx = std::stoi(std::string{term.substr(i, j - i)});
        cb(p, idx);
        i = j;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class CALLBACK> void
_for_each_term_in_file(std::string_view filename,
                        std::string_view key,
                        const CALLBACK& cb)
{
    // Read the raw text dataset from the HDF5 file.
    H5::H5File  hfile(std::string{filename}, H5F_ACC_RDONLY);
    H5::DataSet ds    = hfile.openDataSet(std::string{key});
    H5::DataType dt   = ds.getDataType();

    std::string text;
    ds.read(text, dt);

    // Split `text` on '+' that are NOT inside parentheses.
    // Each chunk corresponds to one Pauli term.
    int depth = 0;
    size_t chunk_start = 0;

    auto process_chunk = [&](size_t start, size_t end)
    {
        // trim leading/trailing whitespace
        while (start < end && std::isspace((unsigned char)text[start]))
            ++start;
        while (end > start && std::isspace((unsigned char)text[end - 1]))
            --end;
        if (start >= end)
            return;

        std::string_view chunk(text.data() + start, end - start);

        // --- parse coefficient ---
        // Format: (real+imagj) or (real-imagj) or just a number
        // We want only the real part.
        size_t ci = 0;
        if (ci < chunk.size() && chunk[ci] == '(')
            ++ci;

        size_t coeff_start = ci;
        // read until we hit '+' (not the sign of a negative real) or 'j' or ')'
        // A leading '-' is part of the number; a '+' after digits ends the real part.
        if (ci < chunk.size() && (chunk[ci] == '-' || chunk[ci] == '+'))
            ++ci;
        while (ci < chunk.size() && chunk[ci] != '+' && chunk[ci] != 'j' && chunk[ci] != ')')
            ++ci;

        double coeff = 0.0;
        try
            coeff = std::stod(std::string{chunk.substr(coeff_start, ci - coeff_start)});
        catch (...)
            return; // malformed term — skip

        // --- parse Pauli string inside [...] ---
        size_t lb = chunk.find('[');
        size_t rb = chunk.find(']');
        if (lb == std::string_view::npos || rb == std::string_view::npos)
            return;

        std::string_view bracket_content = chunk.substr(lb + 1, rb - lb - 1);
        // trim
        size_t bc_start = 0;
        while (bc_start < bracket_content.size() && std::isspace((unsigned char)bracket_content[bc_start]))
            ++bc_start;
        if (bc_start == bracket_content.size())
            return; // identity — skip

        // Build the term string: lowercase "{p}{idx}" tokens separated by spaces.
        std::string term;
        term.reserve(bracket_content.size());
        size_t bi = bc_start;
        while (bi < bracket_content.size())
        {
            while (bi < bracket_content.size() && std::isspace((unsigned char)bracket_content[bi]))
                ++bi;
            if (bi >= bracket_content.size())
                break;

            // first char is the operator (X/Y/Z), rest is the qubit index
            char op = std::tolower((unsigned char)bracket_content[bi++]);
            size_t bj = bi;
            while (bj < bracket_content.size() && !std::isspace((unsigned char)bracket_content[bj]))
                ++bj;

            if (!term.empty())
                term += ' ';
            term += op;
            term += bracket_content.substr(bi, bj - bi);
            bi = bj;
        }

        if (!term.empty())
            cb(term, coeff);
    };

    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '(')
        {
            ++depth;
        }
        else if (text[i] == ')')
        {
            --depth;
        }
        else if (text[i] == '+' && depth == 0)
        {
            process_chunk(chunk_start, i);
            chunk_start = i + 1;
        }
    }
    // process the final chunk (no trailing '+')
    process_chunk(chunk_start, text.size());
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
_trotterization_write_term(std::ostream& ostrm,
                            std::string_view term,
                            double c,
                            std::string_view control,
                            std::string_view qr)
{
    std::vector<int> support;
    support.reserve(term.size()/2);

    // the main parts of the term impl are:
    //  `fwd_basis` -- basis changes before CX + rotation
    //  `bck_basis` -- basis un-changes at the end
    //  `cx_slide` -- the CX gates before and after the rotation
    std::stringstream fwd_basis, bck_basis, cx_slide;

    _for_each_in_pauli_term(term,
                [&fwd_basis, &bck_basis, &support, qr] (char p, int idx)
                {
                    if (p == 'i')
                        return;
                    support.push_back(idx);

                    // write basis-change operations
                    if (p == 'x')
                    {
                        fwd_basis << GATE("h").operand(qr, {idx});
                        bck_basis << GATE("h").operand(qr, {idx});
                    }
                    else if (p == 'y')
                    {
                        fwd_basis << GATE("h").operand(qr, {idx}) << GATE("sxdg").operand(qr, {idx});
                        bck_basis << GATE("sx").operand(qr, {idx}) << GATE("h").operand(qr, {idx});
                    }
                });
    for (int idx : support)
        cx_slide << GATE("cx").operand(qr, {idx, support.back()});

    GATE rotation(control.empty() ? "rz" : "crz");
    rotation.arg(c);
    if (!control.empty())
        rotation.operand(control);
    rotation.operand(qr, {support.back()});

    // write the final circuit:
    ostrm << fwd_basis.str()
            << cx_slide.str()
            << rotation
            << cx_slide.str()
            << bck_basis.str();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace ham
