/*
===========================================================================
Copyright (C) 2024 the OpenMoHAA team

This file is part of OpenMoHAA source code.

OpenMoHAA source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

OpenMoHAA source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenMoHAA source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "tr_threadpool.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_MSC_VER)
#    if defined(_M_ARM64) || defined(_M_ARM)
#        include <intrin.h>
#    elif defined(_M_X64) || defined(_M_IX86)
#        include <immintrin.h>
#    endif
#endif

namespace {

inline void CpuRelax() {
#if defined(_MSC_VER) && (defined(_M_ARM64) || defined(_M_ARM))
    __yield();
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    _mm_pause();
#elif defined(__aarch64__) || defined(__arm__)
    asm volatile("yield" ::: "memory");
#elif defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#endif
}

struct ThreadPoolState {
    std::vector<std::thread> workers;
    std::mutex               wakeMutex;
    std::condition_variable  wakeCv;

    std::atomic<uint32_t>    generation{0};
    std::atomic<bool>        stop{false};
    std::atomic<int>         nextIndex{0};
    std::atomic<int>         activeWorkers{0};

    int                      totalCount{0};
    int                      chunkSize{1};
    r_parallel_fn_t          taskFn{nullptr};
    void                    *taskCtx{nullptr};
    bool                     initialized{false};
};

ThreadPoolState g_pool;

void WorkerLoop() {
    uint32_t localGen = 0;

    while (true) {
        bool foundWork = false;
        for (int spin = 0; spin < 1024; ++spin) {
            if (g_pool.stop.load(std::memory_order_acquire)) {
                return;
            }
            if (g_pool.generation.load(std::memory_order_acquire) != localGen) {
                foundWork = true;
                break;
            }
            CpuRelax();
        }

        if (!foundWork) {
            std::unique_lock<std::mutex> lock(g_pool.wakeMutex);
            g_pool.wakeCv.wait(lock, [&]() {
                return g_pool.stop.load(std::memory_order_acquire)
                    || g_pool.generation.load(std::memory_order_acquire) != localGen;
            });
            if (g_pool.stop.load(std::memory_order_acquire)) {
                return;
            }
        }

        localGen = g_pool.generation.load(std::memory_order_acquire);

        const int             count = g_pool.totalCount;
        const int             chunk = g_pool.chunkSize;
        const r_parallel_fn_t fn    = g_pool.taskFn;
        void                 *ctx   = g_pool.taskCtx;

        while (true) {
            const int start = g_pool.nextIndex.fetch_add(chunk, std::memory_order_relaxed);
            if (start >= count) {
                break;
            }
            const int end = (start + chunk < count) ? (start + chunk) : count;
            fn(start, end, ctx);
        }

        g_pool.activeWorkers.fetch_sub(1, std::memory_order_acq_rel);
    }
}

} // namespace

extern "C" void R_ThreadPoolInit(void) {
    if (g_pool.initialized) {
        return;
    }

    unsigned int hw = std::thread::hardware_concurrency();
    if (hw == 0) {
        hw = 4;
    }
    if (hw > 8) {
        hw = 8;
    }

    const int numWorkers = (hw > 1) ? static_cast<int>(hw - 1) : 0;
    g_pool.stop.store(false, std::memory_order_relaxed);
    g_pool.generation.store(0, std::memory_order_relaxed);
    g_pool.activeWorkers.store(0, std::memory_order_relaxed);
    g_pool.initialized = true;

    g_pool.workers.reserve(numWorkers);
    for (int i = 0; i < numWorkers; ++i) {
        g_pool.workers.emplace_back(WorkerLoop);
    }
}

extern "C" void R_ThreadPoolShutdown(void) {
    if (!g_pool.initialized) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_pool.wakeMutex);
        g_pool.stop.store(true, std::memory_order_release);
    }
    g_pool.wakeCv.notify_all();

    for (auto &t : g_pool.workers) {
        if (t.joinable()) {
            t.join();
        }
    }
    g_pool.workers.clear();
    g_pool.initialized = false;
}

extern "C" int R_ThreadPoolNumThreads(void) {
    if (!g_pool.initialized) {
        return 1;
    }
    return static_cast<int>(g_pool.workers.size()) + 1;
}

extern "C" void R_ParallelFor(int count, int minGrain, r_parallel_fn_t fn, void *ctx) {
    if (count <= 0 || !fn) {
        return;
    }
    if (minGrain < 1) {
        minGrain = 1;
    }

    const int numWorkers = g_pool.initialized ? static_cast<int>(g_pool.workers.size()) : 0;
    if (numWorkers <= 0 || count <= minGrain) {
        fn(0, count, ctx);
        return;
    }

    const int totalThreads = numWorkers + 1;
    int       chunk        = (count + totalThreads * 2 - 1) / (totalThreads * 2);
    if (chunk < minGrain) {
        chunk = minGrain;
    }

    g_pool.totalCount = count;
    g_pool.chunkSize  = chunk;
    g_pool.taskFn     = fn;
    g_pool.taskCtx    = ctx;
    g_pool.nextIndex.store(0, std::memory_order_relaxed);
    g_pool.activeWorkers.store(numWorkers, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(g_pool.wakeMutex);
        g_pool.generation.fetch_add(1, std::memory_order_release);
    }
    g_pool.wakeCv.notify_all();

    while (true) {
        const int start = g_pool.nextIndex.fetch_add(chunk, std::memory_order_relaxed);
        if (start >= count) {
            break;
        }
        const int end = (start + chunk < count) ? (start + chunk) : count;
        fn(start, end, ctx);
    }

    while (g_pool.activeWorkers.load(std::memory_order_acquire) > 0) {
        CpuRelax();
    }
}
