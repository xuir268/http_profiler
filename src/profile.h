#pragma once
#include <iostream>



namespace CallStack {
    inline uint64_t rdtsc() {
    #if defined(__has_builtin)
        #if __has_builtin(__builtin_readcyclecounter)
            return __builtin_readcyclecounter();
        #else
         using Clock = std::chrono::steady_clock;
         auto now = Clock::now().time_since_epoch();
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
        #endif
    #else
     using Clock = std::chrono::steady_clock;
     auto now = Clock::now().time_since_epoch();
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
    #endif
    }
    
    struct ThreadState {
        uint32_t tid{0};
        int last_core{-1};
        uint64_t last_update_ts{0};

        // Optional user-friendly name for UI (Orbit-style)
        std::string thread_name;

        std::vector<std::string> callstack_copy;

        // HFT-style metrics
        uint32_t migration_count{0}; // how many times we jumped cores
        uint64_t stall_cycles{0};    // reserved for kernel / stall time if you sample it later
    };

    struct GpuScope {
        explicit GpuScope(uint64_t start_ts) {}
        ~GpuScope() {}
    };

};

    #define DYNAMIC_PROBE(name) \
        __asm__ __volatile__("nop" ::: "memory");

