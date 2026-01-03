#pragma once
#include <string>
#include <cstdint>
#include <memory>
#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include "worker_pool.h"
#include "context_pool.h"

namespace asio = boost::asio;
namespace http = boost::beast::http;
namespace beast = boost::beast;
using tcp = boost::asio::ip::tcp;

class Listener : public std::enable_shared_from_this<Listener> {
private:
    void do_accept();
    
    tcp::acceptor acceptor_;
    WorkerPool& workers_;   
    contextPool ctx_pool_;
    std::atomic<uint64_t> request_id_counter_{1};
public:
    Listener(asio::io_context& ioc, tcp::endpoint endpoint, 
        WorkerPool& workers,
        contextPool ctx_pool);
        
    void run();
};
