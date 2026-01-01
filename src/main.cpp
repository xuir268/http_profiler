#include<iostream>
#include<thread>
#include <boost/asio.hpp>

namespace mainServer{

};

int main(int argc, char** argv) {
    const uint16_t port = (argc >=2) ? static_cast<uint16_t>(std::stoi(argv[1])) : 8080;

    const size_t hw = std::max(1u, std::thread::hardware_concurrency());
    const size_t io_threads = std::max<size_t>(1, hw / 4);
    const size_t worker_threads = std::max<size_t>(1, hw - io_threads);

    boost::asio::io_context io_context(io_threads + worker_threads);
    auto guard = boost::asio::make_work_guard(io_context);

    //contextPool ctx_pool(8192);
    //WorkerPool workers(worker_threads);

    auto listner = std::make_shared<Listener>(
        io_context, 
        port, 
        workers
        );
    listner->run();
    std::vector<std::thread> threads;
    for (size_t = 0; i < io_threads; ++i) {
        threads.emplace_back([&io_context]() {
            io_context.run();
        });
    }
    return 0;
}
