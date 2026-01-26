#pragma once
#include <iostream>
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>


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

        uint32_t migration_count{0}; // how many times we jumped cores
        uint64_t stall_cycles{0};    // reserved for kernel / stall time if you sample it later
    };

    struct GpuScope {
        explicit GpuScope(uint64_t start_ts) {}
        ~GpuScope() {}
    };



    #define DYNAMIC_PROBE(name) \
        __asm__ __volatile__("nop" ::: "memory");

class TelemetryHub {
    private:
        struct Registry {
            std::mutex mtx;
            std::unordered_map<std::thread::id, ThreadState> thread_map;
        };

        std::array<Registry,2> buffers_{};
        std::atomic<uint8_t> active_idx_{0};

        TelemetryHub() = default;
    public:
        static TelemetryHub& instance() {
            static TelemetryHub hub;
            return hub;
        }
        
        void set_thread_name(const std::string& name) {
           auto& active = buffers_[active_idx_.load(std::memory_order_relaxed)];
           std::lock_guard lock(active.mtx);
           auto tid = std::this_thread::get_id();
           auto& state = active.thread_map[tid];
           state.thread_name = name;
        }

        void push_state(const std::vector<std::string>& stack) {

        }
};
}