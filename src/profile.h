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

    enum class Phase : uint8_t { Begin, End, Instant, Counter };

    struct TraceEvent {
        uint64_t ts_us;
        uint32_t tid;
        uint32_t name_id;
        Phase phase;
        int64_t value;
        int cpu_core;
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
    };

    template <size_t N>
    struct ThreadRing {
    static_assert((N & (N - 1)) == 0, "N must be power of two for masking");
    
    // Use atomics for head/tail to make it visible across threads without mutexes
    alignas(64) std::atomic<uint32_t> head{0}; 
    alignas(64) TraceEvent buf[N];

    // Worker pushes here (Zero Locks)
    void push(const TraceEvent& e) {
        uint32_t h = head.load(std::memory_order_relaxed);
        buf[h & (N - 1)] = e;
        // Release ensures the event is written before the head increments
        head.store(h + 1, std::memory_order_release);
        }
    };  

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