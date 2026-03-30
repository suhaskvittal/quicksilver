/*
 *  author: Suhas Vittal
 *  date:   29 March 2026
 * */

#include "hamiltonian.h"

#include "argparse/argparse.h"

#include <curl/curl.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

static constexpr std::string_view HAMLIB_BASE_URL =
    "https://portal.nersc.gov/cfs/m888/dcamps/hamlib/";

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * libcurl write callback: appends received data to a std::string.
 * */
size_t
_curl_write_string(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

/*
 * libcurl write callback: writes received data to an ofstream.
 * */
size_t
_curl_write_file(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* out = static_cast<std::ofstream*>(userdata);
    out->write(ptr, static_cast<std::streamsize>(size * nmemb));
    return size * nmemb;
}

/*
 * Downloads the contents of `url` into a std::string and returns it.
 * Throws on HTTP or network error.
 * */
std::string
_download_to_string(std::string_view url)
{
    CURL* curl = curl_easy_init();
    if (!curl)
        throw std::runtime_error("curl_easy_init failed");

    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL,           std::string{url}.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _curl_write_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        throw std::runtime_error(std::string{"curl error: "} + curl_easy_strerror(res));

    return body;
}

/*
 * Downloads the contents of `url` to a temporary file.
 * Returns the path to the temp file.
 * The caller is responsible for deleting the file.
 * */
std::string
_download_to_temp(std::string_view url)
{
    // build a unique temp path
    std::string tmpl = (std::filesystem::temp_directory_path() / "bisquit_XXXXXX").string();
    int fd = mkstemp(tmpl.data());
    if (fd < 0)
        throw std::runtime_error("mkstemp failed");
    close(fd);

    std::ofstream ofs(tmpl, std::ios::binary | std::ios::trunc);
    if (!ofs)
        throw std::runtime_error("could not open temp file: " + tmpl);

    CURL* curl = curl_easy_init();
    if (!curl)
        throw std::runtime_error("curl_easy_init failed");

    curl_easy_setopt(curl, CURLOPT_URL,           std::string{url}.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _curl_write_file);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &ofs);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    ofs.close();

    if (res != CURLE_OK)
    {
        std::filesystem::remove(tmpl);
        throw std::runtime_error(std::string{"curl error: "} + curl_easy_strerror(res));
    }

    return tmpl;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct HAMILTONIAN_STATS
{
    size_t nqubits{0};
    size_t terms{0};
    double one_norm{0.0};
};

/*
 * Splits a CSV line on commas, returning trimmed fields.
 * */
std::vector<std::string>
_csv_split(const std::string& line)
{
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ','))
    {
        // trim whitespace
        size_t s = field.find_first_not_of(" \t\r\n");
        size_t e = field.find_last_not_of(" \t\r\n");
        fields.push_back(s == std::string::npos ? "" : field.substr(s, e - s + 1));
    }
    return fields;
}

/*
 * Parses the CSV text and returns the row matching `hdf5_filename` and `dataset_key`.
 * CSV columns: File, Dataset, nqubits, terms, one_norm
 * */
HAMILTONIAN_STATS
_read_stats_from_csv(std::string_view csv_text,
                     std::string_view hdf5_filename,
                     std::string_view dataset_key)
{
    std::istringstream ss{std::string{csv_text}};
    std::string line;

    // skip header row
    std::getline(ss, line);

    while (std::getline(ss, line))
    {
        auto fields = _csv_split(line);
        if (fields.size() < 5)
            continue;
        if (fields[0] != hdf5_filename || fields[1] != dataset_key)
            continue;

        HAMILTONIAN_STATS stats;
        stats.nqubits  = static_cast<size_t>(std::stoul(fields[2]));
        stats.terms    = static_cast<size_t>(std::stoul(fields[3]));
        stats.one_norm = std::stod(fields[4]);
        return stats;
    }

    throw std::runtime_error(
        "could not find entry for file '" + std::string{hdf5_filename}
        + "' and key '" + std::string{dataset_key} + "' in the HAMLib CSV");
}

/*
 * Given a path like "chemistry/electronic/standard/H2.hdf5",
 * returns the URL of the CSV metadata file in the same directory.
 * The CSV name is constructed by replacing '/' in the directory path with '_'.
 * */
std::string
_csv_url_for(std::string_view hamlib_path)
{
    // separate directory and filename
    size_t slash = hamlib_path.rfind('/');
    std::string dir{hamlib_path.substr(0, slash)};        // e.g. "chemistry/electronic/standard"

    // build CSV filename: replace '/' with '_'
    std::string csv_name = dir;
    for (char& c : csv_name)
        if (c == '/') c = '_';
    csv_name += ".csv";                                   // e.g. "chemistry_electronic_standard.csv"

    return std::string{HAMLIB_BASE_URL} + dir + "/" + csv_name;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main(int argc, char* argv[])
{
    std::string  algorithm;
    std::string  output_file;
    std::string  hamlib_path;
    std::string  hamlib_key;
    int64_t      trotter_steps;
    bool         qpe;

    ARGPARSE()
        .required("algorithm",
                  "Algorithm to run: trotterization | qubitization",
                  algorithm)
        .optional("-o",  "--out",
                  "Output QASM file",
                  output_file, std::string{"bisquit.out.qasm"})
        .optional("-H",  "--hamiltonian",
                  "Path to HDF5 file relative to the HAMLib FTP root",
                  hamlib_path, std::string{""})
        .optional("",    "--hamlib-key",
                  "HDF5 dataset key, e.g. /ham_BK120",
                  hamlib_key, std::string{""})
        .optional("",    "--trotter-steps",
                  "Number of Trotter steps (divides each term's coefficient)",
                  trotter_steps, 10)
        .optional("",    "--qpe",
                  "Use controlled rotations (CRZ) for QPE",
                  qpe, false)
        .parse(argc, argv);

    // validate: Hamiltonian algorithms require --hamiltonian and --hamlib-key
    if (algorithm == "trotterization" || algorithm == "qubitization")
    {
        if (hamlib_path.empty())
            throw std::runtime_error("'" + algorithm + "' requires --hamiltonian");
        if (hamlib_key.empty())
            throw std::runtime_error("'" + algorithm + "' requires --hamlib-key");
    }

    ////////////////////////////////////////////////////////////
    // dispatch

    if (algorithm == "trotterization")
    {
        // 1. download the HDF5 file to a temp path
        std::string hdf5_url = std::string{HAMLIB_BASE_URL} + hamlib_path;
        std::cout << "downloading " << hdf5_url << " ...\n";
        std::string temp_hdf5 = _download_to_temp(hdf5_url);

        // ensure the temp file is always cleaned up
        struct _TempGuard
        {
            std::string path;
            ~_TempGuard() { std::filesystem::remove(path); }
        } guard{temp_hdf5};

        // 2. download the CSV and look up statistics
        std::string csv_url = _csv_url_for(hamlib_path);
        std::cout << "fetching statistics from " << csv_url << " ...\n";
        std::string csv_text = _download_to_string(csv_url);

        // extract the basename of the HDF5 path for CSV lookup
        size_t slash     = hamlib_path.rfind('/');
        std::string hdf5_filename{hamlib_path.substr(slash + 1)};

        HAMILTONIAN_STATS stats = _read_stats_from_csv(csv_text, hdf5_filename, hamlib_key);

        // 3. report statistics
        std::cout << "NUM_QUBITS  " << stats.nqubits  << "\n";
        std::cout << "NUM_TERMS   " << stats.terms     << "\n";
        std::cout << "ONE_NORM    " << stats.one_norm  << "\n";

        // 4. run trotterization
        ham::build_trotterization(output_file,
                                  temp_hdf5,
                                  hamlib_key,
                                  stats.nqubits,
                                  static_cast<size_t>(trotter_steps),
                                  stats.one_norm,
                                  qpe);

        std::cout << "wrote " << output_file << "\n";
    }
    else
    {
        throw std::runtime_error("unknown algorithm: " + algorithm);
    }

    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
