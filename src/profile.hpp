#pragma once
#include <chrono>
#include <string_view>

struct Profiler {
    struct Span {
        std::string_view name;
        std::chrono::steady_clock::time_point t0;
        explicit Span(std::string_view name_)
            : name(name_), t0(std::chrono::steady_clock::now()) {}
        ~Span() {   
            (void)name;
           }   
        };
    static Span span(std::string_view name) {
        return Span(name);
    }
};