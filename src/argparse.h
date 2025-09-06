/*
    author: Suhas Vittal
    date:   5 September 2025
*/

#ifndef ARGPARSE_h
#define ARGPARSE_h

#include <algorithm>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct ARGPARSE
{
    std::string usage;

    std::vector<std::string> cli_inputs;
    size_t cli_idx{0};

    ARGPARSE(int argc, char** argv);

    template <class T> void read_required(std::string_view param_name, T&);
    /*
        Usage: all optional params must be prefixed with "--". So, for example, if I have a param named "foo",
            I will pass "foo" as `param_name`. The CLI input should be "--foo bar".
        Set the default value for optionals beforehand.
        Flags only have a single dash.
    */
    template <class T> bool find_optional(std::string_view param_name, T&);
    bool                    find_flag(std::string_view flag_name);
};

template <class T> T convert_string_to_type(std::string);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ARGPARSE::ARGPARSE(int argc, char** argv)
    :cli_inputs(argc)
{
    for (int i = 1; i < argc; i++)
        cli_inputs[i-1] = std::string{argv[i]};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T> void
ARGPARSE::read_required(std::string_view name, T& out)
{
    if (cli_idx >= cli_inputs.size())
        throw std::runtime_error("missing required param: " + std::string{name} + "\n\n" + usage);

    std::string v = cli_inputs[cli_idx];
    if (v == "-h" || v == "--help")
        throw std::runtime_error(usage);

    out = convert_string_to_type<T>(v);
    cli_idx++;
}

template <class T> bool
ARGPARSE::find_optional(std::string_view name, T& out)
{
    std::string match_param = "--" + std::string{name};
    auto it = std::find(cli_inputs.begin(), cli_inputs.end(), match_param);
    if (it != cli_inputs.end())
    {
        std::string v = *(it+1);
        out = convert_string_to_type<T>(v);
        return true;
    }
    else
    {
        return false;
    }
}

bool
ARGPARSE::find_flag(std::string_view name)
{
    std::string match_flag = "-" + std::string{name};
    auto it = std::find(cli_inputs.begin(), cli_inputs.end(), match_flag);
    return it != cli_inputs.end();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T> T
convert_string_to_type(std::string s)
{
    if constexpr (std::is_integral<T>::value)
        return static_cast<T>(std::stoll(s));
    else if constexpr (std::is_floating_point<T>::value)
        return static_cast<T>(std::stof(s));
    else
        return s;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif  // ARGPARSE_h