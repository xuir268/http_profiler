#include <iostream>
#include <thread>
#include <vector>
#include <algorithm>
#include <boost/asio.hpp>
#include "app.h"
#include "http_server.h"
#include "context_pool.h"
#include "worker_pool.h"

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

int main(int argc, char** argv) {
    const uint16_t port = (argc >= 2) ? static_cast<uint16_t>(std::stoi(argv[1])) : 8080;

    const size_t hw = std::max(1u, std::thread::hardware_concurrency());
    const size_t io_threads = std::max<size_t>(1, hw / 4);
    const size_t worker_threads = std::max<size_t>(1, hw - io_threads);

    asio::io_context io_context(static_cast<int>(io_threads));
    auto guard = asio::make_work_guard(io_context);

    App app;
    contextPool ctx_pool(8192);
    WorkerPool workers(worker_threads);

    auto endpoint = tcp::endpoint(tcp::v4(), port);
    auto listener = std::make_shared<Listener>(io_context, endpoint, app, workers, ctx_pool);
    listener->run();

    std::vector<std::thread> threads;
    threads.reserve(io_threads);
    for (size_t i = 0; i < io_threads; ++i) {
        threads.emplace_back([&io_context]() {
            io_context.run();
        });
    }

    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    return 0;
}
