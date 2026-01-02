#pragma once
#include <thread>
#include <vector>
#include <string>
#include <atomic>
#include <cstdint>
#include <memory>

namespace profiler {

struct ReqContext {
    uint64_t request_id = 0;
    std::string method;
    std::string target;
    uint32_t content_lenghth = 0;

    std::string scratch;
    void reset() {
        request_id = 0;
        method = {};
        target = {};
        content_lenghth = 0;
        scratch.clear();
    }
};

// Ring buffer pool - data-oriented, zero-copy design
// Contexts stored contiguously, accessed in FIFO order
class contextPool {
private:
    std::vector<ReqContext> pool_;  // Contiguous memory, cache-friendly
    std::atomic<size_t> head_{0};   // Next available slot
    std::atomic<size_t> tail_{0};   // Last released slot
    size_t capacity_;
    
public:
    explicit contextPool(size_t capacity) : capacity_(capacity) {
        pool_.resize(capacity);  // Pre-allocate all contexts
    }
  
    // Acquire a context from the ring buffer
    // Returns pointer to next available context (FIFO order)
    ReqContext* acquire(){
        size_t current_head = head_.load(std::memory_order_relaxed);
        size_t next_head = (current_head + 1) % capacity_;
        
        // Check if buffer is full
        if (next_head == tail_.load(std::memory_order_relaxed)) {
            // Buffer exhausted, allocate new context (fallback)
            static thread_local std::vector<ReqContext> overflow;
            overflow.emplace_back();
            return &overflow.back();
        }
        
        head_.store(next_head, std::memory_order_relaxed);
        pool_[current_head].reset();
        return &pool_[current_head];
    }
    
    // Release a context back to the pool
    void release() {
        size_t next_tail = (tail_.load(std::memory_order_relaxed) + 1) % capacity_;
        tail_.store(next_tail, std::memory_order_relaxed);
    }
    
    ~contextPool() = default;
};

}  // namespace profiler
