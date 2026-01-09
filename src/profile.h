#pragma once
#include <vector>
#include <string>
#include <string_view>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <map>
#include <thread>
#include <sstream>
#include <array>

// For Linux-specific CPU/Thread telemetry
#ifdef __linux__
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

// Placeholder for Vulkan headers (if available)
// #include <vulkan/vulkan.h>

/**
 * @brief Orbital Profiler V3 (Full-Stack Observability)
 * Integrates CPU, Kernel (eBPF-ready), GPU (Vulkan), and Network tracing.
 */
namespace CallStack {

    // --- 1. Zero-Overhead Hardware Clock ---
    inline uint64_t rdtsc() {
        return __builtin_readcyclecounter();
    }

    // --- 2. Advanced Thread State (Kernel & Migration Awareness) ---
    struct ThreadState {
        uint32_t tid;
        int last_core;
        uint64_t last_update_ts;
        std::vector<std::string> callstack_copy;
        
        // HFT Metrics
        uint32_t migration_count{0}; // Track if thread jumps cores
        uint64_t stall_cycles{0};    // Potential kernel-mode time
        
        // Network Stats per Thread
        uint64_t bytes_sent{0};
        uint64_t bytes_received{0};
        uint64_t last_packet_latency_ns{0};
    };

    // --- 3. Vulkan GPU Tracer (AMD/Generic) ---
    struct GpuScope {
        GpuScope(const char* label_name) {
            // Implementation: vkCmdBeginDebugUtilsLabelEXT
        }
        ~GpuScope() {
            // Implementation: vkCmdEndDebugUtilsLabelEXT
        }
    };

    // --- 4. Network Profiler (Socket-level Telemetry) ---
    /**
     * @brief Tracks network I/O duration and throughput.
     * Useful for identifying TCP backpressure or NIC saturation.
     */
    struct NetworkScope {
        const char* op_name;
        uint64_t start_ns;
        size_t* bytes_ptr;

        NetworkScope(const char* name, size_t& bytes_counter) 
            : op_name(name), bytes_ptr(&bytes_counter) {
            start_ns = rdtsc();
        }

        ~NetworkScope() {
            uint64_t delta = rdtsc() - start_ns;
            // Update thread-local or global registry here if needed
        }
    };

    // --- 5. Dynamic Instrumentation Wrapper (eBPF/Uprobes) ---
    #define DYNAMIC_PROBE(name) \
        __asm__ __volatile__ ("nop" : : : "memory"); 

    // --- 6. The Telemetry Hub ---
    class TelemetryHub {
    private:
        struct Registry {
            std::mutex mtx;
            std::map<std::thread::id, ThreadState> thread_map;
        };

        std::array<Registry, 2> buffers_;
        std::atomic<uint8_t> active_idx_{0};

    public:
        static TelemetryHub& instance() {
            static TelemetryHub hub;
            return hub;
        }

        /**
         * @brief PUSH Mechanism: Syncs thread-local state to the active buffer.
         */
        void push_state(std::vector<std::string_view>& stack) {
            auto& active = buffers_[active_idx_.load(std::memory_order_relaxed)];
            std::lock_guard<std::mutex> lock(active.mtx);
            
            auto id = std::this_thread::get_id();
            auto& state = active.thread_map[id];
            
            int current_core = -1;
#ifdef __linux__
            current_core = sched_getcpu();
#endif
            if (state.last_core != -1 && state.last_core != current_core) {
                state.migration_count++;
            }

            state.last_core = current_core;
            state.last_update_ts = rdtsc();
            
            state.callstack_copy.clear();
            for(auto s : stack) state.callstack_copy.emplace_back(s);
        }

        /**
         * @brief Specialized update for network metrics without full stack push
         */
        void update_network(size_t sent, size_t received, uint64_t latency) {
            auto& active = buffers_[active_idx_.load(std::memory_order_relaxed)];
            std::lock_guard<std::mutex> lock(active.mtx);
            auto& state = active.thread_map[std::this_thread::get_id()];
            state.bytes_sent += sent;
            state.bytes_received += received;
            state.last_packet_latency_ns = latency;
        }

        std::string collect_and_flush() {
            uint8_t old_idx = active_idx_.load();
            active_idx_.store(old_idx ^ 1, std::memory_order_release);

            auto& data_source = buffers_[old_idx];
            std::lock_guard<std::mutex> lock(data_source.mtx);

            std::stringstream ss;
            ss << "{\"ts\":" << rdtsc() << ",\"threads\":[";
            bool first = true;
            for (auto const& [id, state] : data_source.thread_map) {
                if (!first) ss << ",";
                ss << "{\"tid\":" << std::hash<std::thread::id>{}(id) 
                   << ",\"core\":" << state.last_core 
                   << ",\"migrations\":" << state.migration_count
                   << ",\"net\":{\"tx\":" << state.bytes_sent << ",\"rx\":" << state.bytes_received << ",\"lat\":" << state.last_packet_latency_ns << "}"
                   << ",\"stack\":[";
                for (size_t i = 0; i < state.callstack_copy.size(); ++i) {
                    ss << "\"" << state.callstack_copy[i] << "\"" << (i == state.callstack_copy.size() - 1 ? "" : ",");
                }
                ss << "]}";
                first = false;
            }
            ss << "]}";
            return ss.str();
        }
    };

    // --- 7. RAII Frames ---
    inline std::vector<std::string_view>& get_local_stack() {
        thread_local std::vector<std::string_view> stack;
        return stack;
    }

    struct ProfileFrame {
        explicit ProfileFrame(std::string_view name) {
            get_local_stack().push_back(name);
            TelemetryHub::instance().push_state(get_local_stack());
        }
        ~ProfileFrame() {
            get_local_stack().pop_back();
            TelemetryHub::instance().push_state(get_local_stack());
        }
    };
}

// --- Global API Macros ---

// Standard CPU Scope
#define PROFILE_SCOPE(name) CallStack::ProfileFrame __frame_##__LINE__(name)

// GPU Command Buffer Scope
#define GPU_SCOPE(label) CallStack::GpuScope __gpu_##__LINE__(label)

// Network I/O Scope
#define NET_SCOPE(name, byte_count) CallStack::NetworkScope __net_##__LINE__(name, byte_count)

// Dynamic Probe Point (Attach eBPF here)
#define PROBE_POINT(name) DYNAMIC_PROBE(name)

// Logic to flush to the visualizer
#define TELEMETRY_FLUSH() CallStack::TelemetryHub::instance().collect_and_flush()