#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

inline constexpr int kChunks = 6;
inline constexpr const char* kScenarioNames[3] = {"a", "b", "c"};

inline void generate(const std::string& dir, long long total) {
    static const double w[3][5] = {
        {25, 5, 5, 25, 40},    // a) task frequencies
        {20, 20, 20, 20, 20},  // b) equal frequencies
        {2, 45, 40, 3, 10},    // c) far from task
    };
    std::filesystem::create_directories(dir);
    const long long per = total / kChunks;
    for (int s = 0; s < 3; ++s) {
        for (int c = 0; c < kChunks; ++c) {
            std::mt19937 rng(1000u * (s + 1) + c);
            std::discrete_distribution<int> pick(std::begin(w[s]), std::end(w[s]));
            std::ofstream out(dir + "/" + kScenarioNames[s] + "_" + std::to_string(c) + ".txt",
                              std::ios::binary);
            for (long long i = 0; i < per; ++i) {
                switch (pick(rng)) {
                    case 0: out << "read 0\n"; break;
                    case 1: out << "write 0 1\n"; break;
                    case 2: out << "read 1\n"; break;
                    case 3: out << "write 1 1\n"; break;
                    default: out << "string\n"; break;
                }
            }
        }
    }
}
