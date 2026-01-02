#pragma once
#include <boost/asio.hpp>
#include <thread>
#include <vector>
#include <utility>

namespace asio = boost::asio;

class WorkerPool {
private:
    asio::io_context ctx_;
    asio::executor_work_guard<asio::io_context::executor_type> guard_;
    std::vector<std::thread> threads;
public:
    explicit WorkerPool(size_t n) : ctx_(static_cast<int>(n)),
          guard_(asio::make_work_guard(ctx_)){
            threads.reserve(
                n);
            for (size_t i = 0; i < n; ++i) {
                threads.emplace_back([this]() {
                    ctx_.run();
                });
            }
          };

    ~WorkerPool();
};


