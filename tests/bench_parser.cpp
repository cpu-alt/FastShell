// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Throughput check: parses 64 MiB of mixed output (plain text, SGR colors,
// UTF-8). Prints MiB/s so regressions are easy to spot.
#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <string>

#include "terminal/Screen.h"

TEST(Benchmark, ParserThroughput) {
    std::string chunk;
    for (int i = 0; i < 200; ++i) {
        chunk += "drwxr-xr-x  2 user group 4096 Nov  3 12:00 \x1b[1;34mdirectory_" + std::to_string(i)
               + "\x1b[0m  caf\xc3\xa9 \xe4\xb8\xad\xe6\x96\x87\r\n";
    }
    constexpr std::size_t Target = 64u << 20;
    Screen screen(50, 200, 10000);

    std::size_t total = 0;
    const auto begin = std::chrono::steady_clock::now();
    while (total < Target) {
        screen.feed(chunk.data(), chunk.size());
        total += chunk.size();
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    const double mibs = double(total) / (1 << 20) / seconds;
    std::printf("[ BENCH    ] %.0f MiB parsed in %.2f s = %.0f MiB/s\n", double(total) / (1 << 20), seconds, mibs);
    EXPECT_GT(mibs, 20.0);
}
