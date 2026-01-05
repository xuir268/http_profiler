#pragma once

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <utility>
#include <atomic>
#include <source_location>   

struct CallStack {
  using Clock = std::chrono::steady_clock;

  struct FrameInfo {
    std::string name;
    Clock::time_point start_ts;
  };

private:
  inline static thread_local std::vector<FrameInfo> stack_{};

  inline static std::atomic<bool> enabled_{true};
  inline static std::atomic<bool> print_on_enter_{false};
  inline static std::atomic<bool> print_on_exit_{false};

  static void print_chain(std::ostream& os) {
    for (size_t i = 0; i < stack_.size(); ++i) {
      os << stack_[i].name;
      if (i + 1 != stack_.size()) os << " -> ";
    }
  }

  static void print_indent(std::ostream& os) {
    for (size_t i = 1; i < stack_.size(); ++i) os << "  ";
  }

public:
  static void set_enabled(bool on) { enabled_.store(on, std::memory_order_relaxed); }
  static void set_print_on_enter(bool on) { print_on_enter_.store(on, std::memory_order_relaxed); }
  static void set_print_on_exit(bool on) { print_on_exit_.store(on, std::memory_order_relaxed); }

  using EnterHook = void(*)(std::string_view name, uint32_t depth);
  using ExitHook  = void(*)(std::string_view name, uint32_t depth, uint64_t duration_us);

  inline static EnterHook on_enter_ = nullptr;
  inline static ExitHook  on_exit_  = nullptr;

  static void set_hooks(EnterHook enter, ExitHook exit) {
    on_enter_ = enter;
    on_exit_  = exit;
  }

  struct CallFrame {
    bool active_ = false;

    explicit CallFrame(std::string_view name) {
      if (!enabled_.load(std::memory_order_relaxed)) return;

      active_ = true;
      stack_.push_back(FrameInfo{std::string(name), Clock::now()});

      const uint32_t depth = static_cast<uint32_t>(stack_.size());
      if (on_enter_) on_enter_(stack_.back().name, depth);

      if (print_on_enter_.load(std::memory_order_relaxed)) {
        std::cout << "[Started] ";
        print_chain(std::cout);
        std::cout << "\n";
      }
    }

    ~CallFrame() {
      if (!active_) return;

      auto end = Clock::now();
      auto& top = stack_.back();
      uint64_t dur_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - top.start_ts).count()
      );

      const uint32_t depth = static_cast<uint32_t>(stack_.size());
      if (on_exit_) on_exit_(top.name, depth, dur_us);

      if (print_on_exit_.load(std::memory_order_relaxed)) {
        std::cout << "[Ended] ";
        print_indent(std::cout);
        std::cout << top.name << " (" << dur_us << "us)\n";
      }

      stack_.pop_back();
    }

    CallFrame(const CallFrame&) = delete;
    CallFrame& operator=(const CallFrame&) = delete;

    CallFrame(CallFrame&& other) noexcept : active_(std::exchange(other.active_, false)) {}
    CallFrame& operator=(CallFrame&& other) noexcept {
      if (this != &other) active_ = std::exchange(other.active_, false);
      return *this;
    }
  };

  static CallFrame span(std::string_view name) { return CallFrame{name}; }

  
  static CallFrame span_here(std::source_location loc = std::source_location::current()) {
    return CallFrame{loc.function_name()};
  }

  static size_t depth() { return stack_.size(); }

  static void print_current_path() {
    std::cout << "[Current Path] ";
    print_chain(std::cout);
    std::cout << "\n";
  }

  static const std::vector<FrameInfo>& get_stack() { return stack_; }
};


#define CALLSTACK_SCOPE() \
  auto _callstack_scope_##__LINE__ = CallStack::span_here()

#define CALLSTACK_SCOPE_N(name_literal) \
  auto _callstack_scope_##__LINE__ = CallStack::span(name_literal)