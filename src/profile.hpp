#pragma once
#include <chrono>
#include <string_view>
#include <vector>
#include <string>
#include <iostream>

struct CallStack {
private:
    static thread_local std::vector<std::string> caller_stack;

    static void print_chain() {
        for(size_t i = 0; i < caller_stack.size(); ++i) {
            std::cout << caller_stack[i] << (i == caller_stack.size() - 1 ? "\n" : " -> ");
        }
    }
    public:
        struct CallFrame {
            std::string name_;
            explicit CallFrame(std::string name) : name_(name) {
                caller_stack.push_back(std::string(name_));
                std::cout << "[Started] ";
                print_chain();
                std::cout << std::endl;
            }

        ~CallFrame() {
           if (!caller_stack.empty()) {
            caller_stack.pop_back();
           }
        }
        
        CallFrame(const CallFrame&) = delete;
        CallFrame& operator=(const CallFrame&) = delete;
        };

        static void print() {
            std::cout << "[Current Path]";
            print_chain();
            std::cout << std::endl;
        }
};