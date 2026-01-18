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
#include <mutex>

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
 * Integrates CPU, Kernel (eBPF-ready), and GPU (Vulkan) tracing.
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
    };

    // --- 3. Vulkan GPU Tracer (AMD/Generic) ---
    // Uses VK_EXT_debug_utils for command buffer labeling
    struct GpuScope {
        // Mocking Vulkan handles for logic demonstration
        // VkCommandBuffer cmd; 
        
        GpuScope(const char* label_name) {
            // Implementation: vkCmdBeginDebugUtilsLabelEXT(cmd, &labelInfo);
            // This allows tools like Radeon GPU Profiler (RGP) to see our scopes.
        }
        ~GpuScope() {
            // Implementation: vkCmdEndDebugUtilsLabelEXT(cmd);
        }
    };

    // --- 4. Dynamic Instrumentation Wrapper (eBPF/Uprobes) ---
    /**
     * @brief Marks a location for dynamic instrumentation.
     * On Linux, these can be targeted by 'bcc' or 'bpftrace' 
     * using uprobes without restarting the app.
     */
    #define DYNAMIC_PROBE(name) \
        __asm__ __volatile__ ("nop" : : : "memory"); // NOP instruction for probe attachment

    // --- 5. The Telemetry Hub ---
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
            // Detect Core Migration (HFT Red Flag)
            if (state.last_core != -1 && state.last_core != current_core) {
                state.migration_count++;
            }

            state.last_core = current_core;
            state.last_update_ts = rdtsc();
            
            state.callstack_copy.clear();
            for(auto s : stack) state.callstack_copy.emplace_back(s);
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

    // --- 6. RAII Frames ---
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

    enum class Phase : uint8_t { Begin, End, Instant, Counter };

struct TraceEvent {
    uint64_t ts_us;      // timestamp in microseconds
    uint32_t tid;        // thread id (for Chrome trace "tid")
    uint32_t name_id;    // interned name id
    Phase phase;
    int64_t value;       // for counters / instant metadata (optional)
};

struct NameInterner {
    std::mutex mtx;
    std::vector<std::string> names;
    std::unordered_map<std::string, uint32_t> map;

    uint32_t intern(std::string_view sv) {
        std::lock_guard<std::mutex> g(mtx);
        auto it = map.find(std::string(sv));
        if (it != map.end()) return it->second;
        uint32_t id = (uint32_t)names.size();
        names.emplace_back(sv);
        map.emplace(names.back(), id);
        return id;
    }

    const std::string& get(uint32_t id) const {
        return names[id];
    }
}

// --- Global API Macros ---

// Standard CPU Scope
#define PROFILE_SCOPE(name) CallStack::ProfileFrame __frame_##__LINE__(name)

// GPU Command Buffer Scope
#define GPU_SCOPE(label) CallStack::GpuScope __gpu_##__LINE__(label)

// Dynamic Probe Point (Attach eBPF here)
#define PROBE_POINT(name) DYNAMIC_PROBE(name)

// Logic to flush to the visualizer
#define TELEMETRY_FLUSH() CallStack::TelemetryHub::instance().collect_and_flush()