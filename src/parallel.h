/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_PARALLEL_H
#define JARA_PARALLEL_H

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

// Threads used by parallelFor(), one for each hardware thread
inline unsigned parallelThreads() {
    return std::max(1u, std::thread::hardware_concurrency());
}

// Calls f(begin, end) for consecutive chunks of [0, count) of at most chunkSize, on several threads that take the next
// chunk when they are done with one. Returns when all are done. f must not throw, and the chunks must be independent:
// f may only write what belongs to its own indices.
template<typename F>
void parallelFor(size_t count, F f, size_t chunkSize = 256) {
    chunkSize = std::max<size_t>(1, chunkSize);
    const size_t chunks = (count + chunkSize - 1) / chunkSize;
    const size_t threadCount = std::min<size_t>(parallelThreads(), chunks);
    if(threadCount <= 1) {
        if(count > 0) {
            f(size_t{0}, count);
        }
        return;
    }
    std::atomic<size_t> next{0};
    auto work = [&]() {
        for(size_t chunk = next++; chunk < chunks; chunk = next++) {
            f(chunk * chunkSize, std::min(count, (chunk + 1) * chunkSize));
        }
    };
    std::vector<std::thread> threads;
    threads.reserve(threadCount - 1);
    for(size_t t = 1; t < threadCount; t++) {
        threads.emplace_back(work);
    }
    work();
    for(std::thread &thread : threads) {
        thread.join();
    }
}

#endif // JARA_PARALLEL_H
