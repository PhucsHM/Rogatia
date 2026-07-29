// Rogatia -- standalone perft suite runner.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Usage: run_perft [suite.txt] [max-depth]
//
// Reads "<FEN> ;D1 n ;D2 n ..." lines and checks every listed depth up to
// max-depth (default 6, i.e. every listed depth).  Pass a lower max-depth for
// a quick run.  Exit code 0 iff every checked depth matched.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "../src/attacks.h"
#include "../src/perft.h"
#include "../src/position.h"

namespace {

struct Entry {
    std::string                                fen;
    std::vector<std::pair<int, std::uint64_t>> expected;
};

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
        return "";
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

bool parse_line(const std::string& raw, Entry& out) {
    const std::string line = trim(raw);
    if (line.empty() || line[0] == '#')
        return false;

    const auto first = line.find(';');
    if (first == std::string::npos)
        return false;

    out.fen = trim(line.substr(0, first));
    out.expected.clear();

    std::size_t pos = first;
    while (pos != std::string::npos) {
        const auto next  = line.find(';', pos + 1);
        std::string field = trim(line.substr(pos + 1, next == std::string::npos
                                                          ? std::string::npos
                                                          : next - pos - 1));
        if (field.size() > 1 && (field[0] == 'D' || field[0] == 'd')) {
            const int         depth = std::atoi(field.c_str() + 1);
            const auto        sp    = field.find_first_of(" \t");
            if (sp != std::string::npos && depth > 0)
                out.expected.emplace_back(
                    depth, std::strtoull(field.c_str() + sp + 1, nullptr, 10));
        }
        pos = next;
    }
    return !out.expected.empty();
}

}  // namespace

int main(int argc, char** argv) {
    const std::string path = (argc > 1) ? argv[1] : "tests/perft_suite.txt";
    const int         maxDepth = (argc > 2) ? std::atoi(argv[2]) : 6;

    // C stdio, NOT std::ifstream.  Constructing an ifstream segfaults on the
    // laptop's MinGW toolchain -- std::cout and C stdio both work, only the
    // file streams die.  That is why this gate, standing rule #1 of the whole
    // project, had never once run on the machine that makes most of the
    // commits.  It was not an engine bug and there was no output to say so.
    std::FILE* in = std::fopen(path.c_str(), "r");
    if (!in) {
        std::cerr << "cannot open suite file: " << path << '\n';
        return 2;
    }

    rogatia::init();
    if (!rogatia::verify_slider_tables()) {
        std::cerr << "slider table self-check FAILED\n";
        return 2;
    }

    std::vector<Entry> entries;
    char               buf[512];   // longest suite line is 124 bytes
    Entry              e;
    while (std::fgets(buf, sizeof buf, in))
        if (parse_line(std::string(buf), e))
            entries.push_back(e);
    std::fclose(in);

    int failures = 0, checked = 0;
    std::uint64_t totalNodes = 0;
    const auto    t0 = std::chrono::steady_clock::now();

    for (const Entry& entry : entries) {
        rogatia::Position pos;
        pos.set(entry.fen);

        std::cout << entry.fen << '\n';

        if (pos.fen() != entry.fen)
            std::cout << "  WARNING: fen round-trip differs: " << pos.fen() << '\n';

        for (const auto& [depth, expect] : entry.expected) {
            if (depth > maxDepth)
                continue;

            const auto          s     = std::chrono::steady_clock::now();
            const std::uint64_t nodes = rogatia::perft(pos, depth);
            const auto          ms    = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - s)
                                  .count();

            totalNodes += nodes;
            ++checked;
            const bool ok = (nodes == expect);
            if (!ok)
                ++failures;

            std::cout << "  D" << std::left << std::setw(2) << depth << std::right
                      << std::setw(15) << nodes << "  expected " << std::setw(14) << expect
                      << "  " << std::left << std::setw(4) << (ok ? "OK" : "FAIL")
                      << std::right << std::setw(7) << ms << "ms\n";

            if (!ok) {
                std::cout << "  --- divide at depth " << depth << " ---\n";
                rogatia::perft_divide(pos, depth, std::cout);
                break;  // deeper depths in a broken position tell you nothing new
            }
        }
        std::cout << '\n';
    }

    const auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - t0)
                             .count();

    // One stream for the whole report, deliberately.  This used to print the
    // per-depth lines and the verdict with std::printf while the FENs went to
    // std::cout, and the two carry separate buffers: redirected to a file the
    // verdict landed nowhere near the end, so `tail` on the log showed a FEN
    // and no result.  A gate whose output reads as a failure is worse than no
    // gate, which is the same lesson that made this runner drop std::ifstream.
    std::cout << checked - failures << '/' << checked << " checks passed, " << totalNodes
              << " nodes in " << totalMs << "ms";
    if (totalMs > 0)
        std::cout << " (" << totalNodes / (std::uint64_t) totalMs << " knps)";
    std::cout << '\n';

    return failures ? 1 : 0;
}
