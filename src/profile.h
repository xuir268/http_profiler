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

        void push_state(const std::vector<std::string_view>& stack) {
            auto& active = buffers_[active_idx_.load(std::memory_order_relaxed)];
            std::lock_guard<std::mutex> lock(active.mtx);

            auto id = std::this_thread::get_id();
            auto& state = active.thread_map[id];

            // Thread id hash for UI
            if (state.tid == 0) {
                // Use hash of std::thread::id as a stable-ish fake tid
                state.tid = static_cast<uint32_t>(std::hash<std::thread::id>{}(id));
            }

            int current_core = -1;
        #ifdef __linux__
            current_core = sched_getcpu();
        #endif

            // Detect Core Migration (HFT-red-flag)
            if (state.last_core != -1 && state.last_core != current_core) {
                state.migration_count++;
            }

            state.last_core = current_core;
            state.last_update_ts = rdtsc();

            state.callstack_copy.clear();
            state.callstack_copy.reserve(stack.size());
            for (auto s : stack) {
                state.callstack_copy.emplace_back(s);
            }
        }
        
        inline std::vector<std::string>& get_local_stack() {
            thread_local std::vector<std::string> local_stack;
            return local_stack;
        }

        struct ProfileFrame {
            explicit ProfileFrame(std::string_view name) {
                auto& stack = TelemetryHub::instance().get_local_stack();
                stack.push_back(std::string(name));
                TelemetryHub::instance().push_state(stack);
            }
            ~ProfileFrame() {
                auto& stack = TelemetryHub::instance().get_local_stack();
                if (!stack.empty()) {
                    stack.pop_back();
                }
            }
        };
};

    // Alias for backward compatibility
    using CallFrame = TelemetryHub::ProfileFrame;



}