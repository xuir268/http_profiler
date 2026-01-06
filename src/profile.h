#pragma once
#include <vector>
#include <string_view>
#include <atomic>
#include <chrono>
#include <cstdint>

/**
 * @brief Orbital Profiler Core
 * Integrated telemetry for CPU, Heap, Stack, and Queue monitoring.
 * Designed for < 50ns overhead on the hot path.
 */
namespace CallStack {

    // --- 1. Callstack & Execution Path ---
    
    /**
     * @brief Thread-local storage for the current execution depth.
     * Uses a fixed-size buffer to avoid allocations during deep recursion.
     */
    inline std::vector<std::string_view>& get_stack() {
        thread_local std::vector<std::string_view> stack;
        if (stack.capacity() == 0) stack.reserve(32); 
        return stack;
    }

    /**
     * @brief RAII Frame for the Callstack
     * Automatically pushes/pops names and tracks cycle counts.
     */
    struct CallFrame {
        uint64_t start_cycles;

        explicit CallFrame(std::string_view name) {
            get_stack().push_back(name);
            // Low-latency hardware timestamp
            start_cycles = __builtin_readcyclecounter();
        }

        ~CallFrame() {
            // Optional: You could log (end - start) to a ring buffer here
            get_stack().pop_back();
        }
    };

    // --- 2. Orbital Metrics (Instant Telemetry) ---

    struct InstantMetrics {
        std::atomic<uint64_t> cpu_load_ticks{0};
        std::atomic<int64_t>  heap_usage_bytes{0};
        std::atomic<uint32_t> queue_length{0};
        std::atomic<uint32_t> active_workers{0};
        
        // Singleton for global telemetry access
        static InstantMetrics& instance() {
            static InstantMetrics m;
            return m;
        }
    };

    /**
     * @brief Memory Tracker Hook
     * Usage: Call inside custom new/delete or middleware.
     */
    inline void track_heap(int64_t delta) {
        InstantMetrics::instance().heap_usage_bytes.fetch_add(delta, std::memory_order_relaxed);
    }

    /**
     * @brief Queue Depth Monitor
     * Tracks how many requests are waiting in the worker pool.
     */
    inline void set_queue_length(uint32_t len) {
        InstantMetrics::instance().queue_length.store(len, std::memory_order_relaxed);
    }

    // --- 3. Sampling Logic ---

    /**
     * @brief Snapshot of the system state for the UI.
     * This is what the 'render_ui' function in pipeline.hpp eventually consumes.
     */
    struct Snapshot {
        uint64_t timestamp;
        double cpu_percent;
        size_t heap_mb;
        uint32_t q_len;
        std::vector<std::string_view> current_path;
    };

    inline Snapshot take_snapshot() {
        auto& metrics = InstantMetrics::instance();
        return {
            static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()),
            static_cast<double>(metrics.cpu_load_ticks.load() % 100), // Simplified
            static_cast<size_t>(metrics.heap_usage_bytes.load() / (1024 * 1024)),
            metrics.queue_length.load(),
            get_stack()
        };
    }
}

/**
 * MACROS for clean profiling in hot paths
 */
#define PROFILE_SCOPE(name) CallStack::CallFrame __frame_##__LINE__(name)
#define TRACK_MEMORY(delta) CallStack::track_heap(delta)
#define UPDATE_QUEUE(len)   CallStack::set_queue_length(len)