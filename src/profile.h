#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
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
    

    struct GpuScope {
        explicit GpuScope(uint64_t start_ts) {}
        ~GpuScope() {}
    };

    #define DYNAMIC_PROBE(name) \
        __asm__ __volatile__("nop" ::: "memory");

    #define TRACE_SCOPE(name) CallStack::ProfileFrame frame_##__LINE__(name)
    #define PROFILE_SCOPE(name) CallStack::ProfileFrame frame_##__LINE__(name)
    #define NET_SCOPE(name, bytes) CallStack::ProfileFrame frame_##__LINE__(name)
    #define PROBE_POINT(name) DYNAMIC_PROBE(name)
    #define GPU_SCOPE(name) auto _gpu_scope = CallStack::GpuScope(CallStack::TelemetryHub::instance().get_timestamp());
    #define TELEMETRY_FLUSH() ("{}")

class TelemetryHub {
    private:
        struct Registry {
            NameInterner interner;
            std::mutex rg_mtx;
            std::vector<ThreadRing<8192>*> all_rings;
        };
        std::array<Registry,2> buffers_{};
        std::atomic<uint8_t> active_idx_{0};

        TelemetryHub() = default;

        Registry& registry() {
            return buffers_[active_idx_.load(std::memory_order_relaxed)];
        }

        const Registry& registry() const {
            return buffers_[active_idx_.load(std::memory_order_relaxed)];
        }
    public:
        static TelemetryHub& instance() {
            static TelemetryHub hub;
            return hub;
        }

        static ThreadRing<8192>& get_tls_ring() {
        thread_local ThreadRing<8192> ring;
        thread_local bool registered = []() {
            auto& inst = TelemetryHub::instance();
            auto& reg = inst.registry();
            std::lock_guard<std::mutex> lock(reg.rg_mtx);
            reg.all_rings.push_back(&ring);
            return true;
        }();
        return ring;
    }

    uint32_t intern_name(std::string_view name) {
        return registry().interner.intern(name);
    }

    uint64_t get_timestamp() const {
        return rdtsc();
    }

    void update_network(size_t bytes, uint64_t latency, uint64_t bandwidth) {
        // Placeholder for network metric updates
    }

    inline std::vector<std::string>& get_local_stack() {
        thread_local std::vector<std::string> local_stack;
        return local_stack;
    }
        
       // High performance recording
    void record(uint32_t name_id, Phase phase, int64_t value = 0) {
        int core = -1;
#ifdef __linux__
        core = sched_getcpu();
#endif
        
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        uint64_t ts = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
        
        get_tls_ring().push({
            ts,
            static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id())),
            name_id,
            phase,
            value,
            core
        });
    }

    // Export function (Can be slow, runs on a different thread)
    std::string export_chrome_trace() {
        std::vector<TraceEvent> events;
        
        {
            auto& reg = registry();
            std::lock_guard<std::mutex> lock(reg.rg_mtx);
            for (auto* ring : reg.all_rings) {
                // Acquire ensures we see the latest writes from that thread
                uint32_t count = ring->head.load(std::memory_order_acquire);
                // Simple logic: grab everything recorded so far
                // In a production system, you'd handle ring-wrap-around here
                for (uint32_t i = 0; i < std::min(count, 8192u); ++i) {
                    events.push_back(ring->buf[i]);
                }
            }
        }

        std::sort(events.begin(), events.end(), [](auto& a, auto& b) {
            return a.ts_us < b.ts_us;
        });

        std::ostringstream os;
        os << "{ \"traceEvents\": [";
        for (size_t i = 0; i < events.size(); ++i) {
            const auto& e = events[i];
            const char* ph = (e.phase == Phase::Begin) ? "B" : (e.phase == Phase::End) ? "E" : "i";
            
            os << (i == 0 ? "" : ",") << "{"
               << "\"ph\":\"" << ph << "\","
               << "\"ts\":" << e.ts_us << ","
               << "\"pid\":1,"
               << "\"tid\":" << e.tid << ","
               << "\"name\":\"" << registry().interner.get(e.name_id) << "\","
               << "\"args\":{\"cpu\":" << e.cpu_core << "}";
            if (e.phase == Phase::Instant) os << ",\"value\":" << e.value;
            os << "}";
        }
        os << "], \"displayTimeUnit\":\"us\" }";
        return os.str();
    }
};

// RAII Scope for profiling
struct ProfileFrame {
    uint32_t id;
    explicit ProfileFrame(std::string_view name) {
        auto& stack = TelemetryHub::instance().get_local_stack();
        stack.emplace_back(std::string(name));
        id = TelemetryHub::instance().intern_name(name);
        TelemetryHub::instance().record(id, Phase::Begin);
    }
    ~ProfileFrame() {
        TelemetryHub::instance().record(id, Phase::End);
        auto& stack = TelemetryHub::instance().get_local_stack();
        if (!stack.empty()) {
            stack.pop_back();
        }
    }
};

#define TRACE_SCOPE(name) CallStack::ProfileFrame frame_##__LINE__(name)

using CallFrame = ProfileFrame;

inline std::vector<std::string>& get_stack() {
    return TelemetryHub::instance().get_local_stack();
}

inline CallFrame span(std::string_view name) {
    return CallFrame(name);
}

} // namespace CallStack


