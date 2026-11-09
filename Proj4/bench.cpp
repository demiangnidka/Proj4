#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "ds.h"
#include "gen.h"

constexpr long long kTotalOps = 6000000;
static const char* kScenarioDesc[3] = {"task frequencies", "equal frequencies",
                                       "far from task"};

static std::ofstream g_results("results.txt");

static void say(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    std::fputs(buf, stdout);
    g_results << buf;
}

enum OpType : uint8_t { kRead, kWrite, kString };
struct Op {
    int32_t value;
    uint8_t type;
    uint8_t field;
};

static std::vector<Op> load_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "Cannot open %s\n", path.c_str());
        std::exit(1);
    }
    in.seekg(0, std::ios::end);
    std::string buf(static_cast<size_t>(in.tellg()), '\0');
    in.seekg(0);
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));

    std::vector<Op> ops;
    ops.reserve(buf.size() / 8);
    const char* p = buf.c_str();
    const char* end = p + buf.size();
    while (p < end) {
        Op op{0, 0, 0};
        char* q = nullptr;
        if (std::strncmp(p, "read ", 5) == 0) {
            op.type = kRead;
            op.field = static_cast<uint8_t>(std::strtol(p + 5, &q, 10));
        } else if (std::strncmp(p, "write ", 6) == 0) {
            op.type = kWrite;
            op.field = static_cast<uint8_t>(std::strtol(p + 6, &q, 10));
            op.value = static_cast<int32_t>(std::strtol(q, &q, 10));
        } else if (std::strncmp(p, "string", 6) == 0) {
            op.type = kString;
            q = const_cast<char*>(p + 6);
        } else {
            std::fprintf(stderr, "Invalid line in %s\n", path.c_str());
            std::exit(1);
        }
        if (op.type != kString && op.field > 1) {
            std::fprintf(stderr, "Invalid field in %s\n", path.c_str());
            std::exit(1);
        }
        ops.push_back(op);
        p = q;
        while (p < end && *p != '\n') ++p;
        if (p < end) ++p;
    }
    return ops;
}

using Chunks = std::vector<std::vector<Op>>;
using Work = std::vector<const std::vector<Op>*>;

// Runs the files split across T threads (each thread gets its own files).
// Returns the elapsed time in milliseconds.
template <class D>
static double run(const Chunks& chunks, int threads, long long& checksum) {
    std::vector<Work> work(threads);
    const int per = kChunks / threads;
    for (int t = 0; t < threads; ++t)
        for (int k = 0; k < per; ++k) work[t].push_back(&chunks[t * per + k]);

    D data;
    struct alignas(64) Result { long long sum = 0; };
    std::vector<Result> res(threads);

    auto body = [&](int t) {
        long long s = 0;
        for (const auto* part : work[t]) {
            for (const Op& op : *part) {
                switch (op.type) {
                    case kRead: s += data.get(op.field); break;
                    case kWrite: data.set(op.field, op.value); break;
                    default: s += static_cast<std::string>(data).size(); break;
                }
            }
        }
        res[t].sum = s;
    };

    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> pool;
    for (int t = 1; t < threads; ++t) pool.emplace_back(body, t);
    body(0);
    for (auto& th : pool) th.join();
    auto t1 = std::chrono::steady_clock::now();

    checksum = 0;
    for (auto& r : res) checksum += r.sum;
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

template <class D>
static void table(const char* title, const Chunks (&sc)[3], int reps, bool verbose) {
    say("\n=== %s ===\n", title);
    double avg[3][3];
    for (int s = 0; s < 3; ++s) {
        for (int t = 1; t <= 3; ++t) {
            double sum = 0;
            long long cs = 0;
            for (int r = 0; r < reps; ++r) {
                double ms = run<D>(sc[s], t, cs);
                sum += ms;
                if (verbose)
                    say("  file %s, threads %d, run %d: %8.1f ms (checksum %lld)\n",
                        kScenarioNames[s], t, r + 1, ms, cs);
            }
            avg[s][t - 1] = sum / reps;
        }
    }
    say("Average time, ms (%d runs):\n", reps);
    say("%-30s %10s %10s %10s\n", "", "1 thread", "2 threads", "3 threads");
    for (int s = 0; s < 3; ++s) {
        std::string label = std::string(kScenarioNames[s]) + ") " + kScenarioDesc[s];
        say("%-30s %10.1f %10.1f %10.1f\n", label.c_str(), avg[s][0], avg[s][1], avg[s][2]);
    }
}

int main(int argc, char** argv) {
    int reps = 5;
    bool compare = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--compare") == 0) compare = true;
        else reps = std::atoi(argv[i]);
    }
    if (reps < 1) reps = 1;

#ifndef NDEBUG
    std::fprintf(stderr, "WARNING: Debug build. Use Release for timing.\n");
#endif

    const std::string dir = "data";
    if (!std::filesystem::exists(dir + "/a_0.txt")) {
        say("Generating test files in %s ...\n",
            std::filesystem::absolute(dir).string().c_str());
        generate(dir, kTotalOps);
    }

    static Chunks sc[3];
    for (int s = 0; s < 3; ++s) {
        sc[s].resize(kChunks);
        for (int c = 0; c < kChunks; ++c)
            sc[s][c] = load_file(dir + "/" + kScenarioNames[s] + "_" + std::to_string(c) + ".txt");
    }
    say("Files loaded (file reading time is not included)\n");

    table<FieldsData<std::mutex, std::mutex>>("Separate mutex per field", sc, reps, true);

    if (compare) {
        table<GlobalData>("Comparison: one global mutex", sc, reps, false);
        table<FieldsData<std::shared_mutex, std::shared_mutex>>(
            "Comparison: shared_mutex on both fields", sc, reps, false);
        table<FieldsData<std::shared_mutex, std::mutex>>(
            "Comparison: shared_mutex (field 0) + mutex (field 1)", sc, reps, false);
    }

    g_results.close();
    std::printf("\nResults saved to %s\n",
                std::filesystem::absolute("results.txt").string().c_str());
    return 0;
}
