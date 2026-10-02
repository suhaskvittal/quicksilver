/* Author: Codex (GPT-6 Sol), 2 October 2026.
 * Clifford+V Rz lookup table generator; V = diag(1, exp(i*pi/8)).
 * Versioned ASCII gate format is documented in Cyclosynth.txt.
 */
#include <algorithm>
#include <bit>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#ifndef QS_CYCLOSYNTH_BRIDGE
#define QS_CYCLOSYNTH_BRIDGE "deps/cyclosynth/target/release/examples/qs_rz_bridge"
#endif

namespace {
double real(const std::string& s) {
    size_t end{}; const double v = std::stod(s, &end);
    if (end != s.size() || !std::isfinite(v)) throw std::runtime_error("expected finite number: " + s);
    return v;
}
uint64_t integer(const std::string& s) {
    uint64_t v{}; auto [end, error] = std::from_chars(s.data(), s.data()+s.size(), v);
    if (error != std::errc{} || end != s.data()+s.size() || v == 0)
        throw std::runtime_error("expected positive integer: " + s);
    return v;
}
void bytes(FILE* f, const void* p, size_t n) {
    if (std::fwrite(p, 1, n, f) != n) throw std::runtime_error("LUT write failed");
}
template<class T> void little(FILE* f, T v) {
    for (size_t i=0; i<sizeof(T); ++i) { const uint8_t b = v & 255; bytes(f, &b, 1); v >>= 8; }
}
void floating(FILE* f, double v) { static_assert(sizeof(double)==8 && std::numeric_limits<double>::is_iec559); little(f, std::bit_cast<uint64_t>(v)); }

class Backend {
    pid_t pid{-1}; FILE* input{}; FILE* output{};
public:
    Backend(const std::string& path, uint64_t threads) {
        int in[2], out[2];
        if (pipe(in)) throw std::runtime_error("backend input pipe failed");
        if (pipe(out)) { close(in[0]); close(in[1]); throw std::runtime_error("backend output pipe failed"); }
        const auto nt = std::to_string(threads);
        pid = fork();
        if (pid == 0) {
            dup2(in[0], STDIN_FILENO); dup2(out[1], STDOUT_FILENO);
            close(in[0]); close(in[1]); close(out[0]); close(out[1]);
            setenv("RAYON_NUM_THREADS", nt.c_str(), 1);
            execl(path.c_str(), path.c_str(), static_cast<char*>(nullptr));
            std::perror("cannot execute cyclosynth bridge"); _exit(127);
        }
        close(in[0]); close(out[1]);
        if (pid < 0) { close(in[1]); close(out[0]); throw std::runtime_error("backend fork failed"); }
        input = fdopen(in[1], "w"); output = fdopen(out[0], "r");
        if (!input || !output) {
            if (input) fclose(input); else close(in[1]);
            if (output) fclose(output); else close(out[0]);
            kill(pid, SIGTERM); waitpid(pid, nullptr, 0); pid=-1;
            throw std::runtime_error("backend stream open failed");
        }
    }
    ~Backend() { if (input) fclose(input); if (output) fclose(output); if (pid>0) { kill(pid, SIGTERM); waitpid(pid, nullptr, 0); } }
    std::pair<double, std::string> synthesize(double angle, double epsilon) {
        if (fprintf(input, "%.17g %.17g\n", angle, epsilon)<0 || fflush(input)) throw std::runtime_error("backend input failed");
        char* line{}; size_t capacity{}; const auto n = getline(&line, &capacity, output);
        if (n<0) { free(line); throw std::runtime_error("cyclosynth bridge failed; see its stderr"); }
        std::string response(line, n); free(line);
        const auto sep = response.find(' ');
        if (sep == std::string::npos) throw std::runtime_error("invalid backend response");
        const double distance = real(response.substr(0, sep));
        std::string gates = response.substr(sep+1);
        if (!gates.empty() && gates.back()=='\n') gates.pop_back();
        if (gates=="-") gates.clear();
        if (distance<0 || distance>=epsilon || gates.find_first_not_of("HSsXYZVv")!=std::string::npos)
            throw std::runtime_error("backend returned invalid gates or distance");
        return {distance, gates};
    }
    void finish() {
        fclose(input); input=nullptr; fclose(output); output=nullptr;
        int status{}; pid_t result; do { result=waitpid(pid, &status, 0); } while (result<0 && errno==EINTR);
        pid=-1;
        if (result<0 || !WIFEXITED(status) || WEXITSTATUS(status)!=0) throw std::runtime_error("cyclosynth bridge exited unsuccessfully");
    }
};
}

int main(int argc, char** argv) {
    FILE* file{}; std::string temporary;
    try {
        if (argc<5) throw std::runtime_error("usage: qs_build_lut_v lower-bound upper-bound count output [-t|--threads N] [-e|--epsilon E] [--backend PATH]");
        const double lo=real(argv[1]), hi=real(argv[2]); const auto count=integer(argv[3]);
        const std::string destination=argv[4]; uint64_t threads=8; double epsilon=0;
        std::string backend=QS_CYCLOSYNTH_BRIDGE;
        for (int i=5; i<argc; i+=2) {
            if (i+1>=argc) throw std::runtime_error("missing option value");
            const std::string option=argv[i];
            if (option=="-t" || option=="--threads") threads=integer(argv[i+1]);
            else if (option=="-e" || option=="--epsilon") { epsilon=real(argv[i+1]); if (epsilon<1e-48 || epsilon>=1) throw std::runtime_error("require 1e-48 <= epsilon < 1"); }
            else if (option=="--backend") backend=argv[i+1];
            else throw std::runtime_error("unknown option: " + option);
        }
        if (lo>hi || (lo<0 && hi>0) || !std::isfinite(hi-lo)) throw std::runtime_error("require ordered bounds that do not cross zero");
        std::vector<double> angles; angles.reserve(count);
        for (uint64_t i=0; i<count; ++i) angles.push_back(lo + (hi-lo)*(static_cast<double>(i)/static_cast<double>(count)));
        std::stable_sort(angles.begin(), angles.end(), [](double a, double b) { return std::abs(a)<std::abs(b); });
        signal(SIGPIPE, SIG_IGN);
        Backend synth(backend, threads);
        temporary=destination+".tmp.XXXXXX"; const int fd=mkstemp(temporary.data());
        if (fd<0) throw std::runtime_error("cannot create temporary LUT beside output");
        file=fdopen(fd, "wb"); if (!file) { close(fd); throw std::runtime_error("cannot open LUT stream"); }
        const char magic[8]={'Q','S','V','L','U','T','1','\0'}; bytes(file, magic, 8); little(file, count);
        for (size_t i=0; i<angles.size(); ++i) {
            const double angle=angles[i];
            const double eps=epsilon ? epsilon : (angle==0 ? 1e-5 : std::clamp(std::pow(10.0, -std::ceil(-std::log10(std::abs(angle)))-5), 1e-48, 1e-5));
            auto [distance, gates]=synth.synthesize(angle, eps);
            if (gates.size()>UINT32_MAX) throw std::runtime_error("sequence too long");
            floating(file, angle); floating(file, eps); floating(file, distance);
            little(file, static_cast<uint32_t>(gates.size())); bytes(file, gates.data(), gates.size());
            std::cout << "\rprogress: " << i+1 << '/' << count << std::flush;
        }
        synth.finish(); const int closed=fclose(file); file=nullptr;
        if (closed || rename(temporary.c_str(), destination.c_str())) throw std::runtime_error("cannot finish LUT output");
        temporary.clear(); std::cout << "\nqs_build_lut_v: done\n"; return 0;
    } catch (const std::exception& error) {
        if (file) fclose(file);
        if (!temporary.empty()) unlink(temporary.c_str());
        std::cerr << "qs_build_lut_v: " << error.what() << '\n'; return 1;
    }
}
