#include "http_server.h"
#include "profile.h"
#include <memory>

using Request = http::request<http::string_body>;
using Response = http::response<http::string_body>;

namespace {
http::status to_http_status(HttpStatus status) {
    switch (status) {
        case HttpStatus::OK: return http::status::ok;
        case HttpStatus::BadRequest: return http::status::bad_request;
        case HttpStatus::Unauthorized: return http::status::unauthorized;
        case HttpStatus::Forbidden: return http::status::forbidden;
        case HttpStatus::NotFound: return http::status::not_found;
        case HttpStatus::ServiceUnavailable: return http::status::service_unavailable;
        case HttpStatus::InternalServerError:
        default: return http::status::internal_server_error;
    }
}

class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    HttpSession(tcp::socket socket,
                App& app,
                WorkerPool& workers,
                contextPool& ctx_pool,
                std::atomic<uint64_t>& request_id)
        : socket_(std::move(socket)),
          app_(app),
          workers_(workers),
          ctx_pool_(ctx_pool),
          request_id_(request_id) {}

    void run() { do_read(); }

private:
    tcp::socket socket_;
    App& app_;
    WorkerPool& workers_;
    contextPool& ctx_pool_;
    std::atomic<uint64_t>& request_id_;

    beast::flat_buffer buffer_;
    Request req_;

    void do_read();
    void dispatch_to_workers();
    void do_write(std::shared_ptr<Response> res);
    void do_close();
};
} // namespace

void HttpSession::do_read() {
    auto self = shared_from_this();
    http::async_read(socket_, buffer_, req_,
        [self](beast::error_code ec, std::size_t) {
            if (ec == http::error::end_of_stream) {
                return self->do_close();
            }
            if (ec) {
                return;
            }
            self->dispatch_to_workers();
        });
}

void HttpSession::dispatch_to_workers() {
    auto self = shared_from_this();
    auto req_copy = std::make_shared<Request>(std::move(req_));
    req_ = {};

    workers_.post([self, req_copy]() {
        auto span = CallStack::span("worker.request");

        auto ctx = self->ctx_pool_.acquire();
        ctx->request_id = self->request_id_.fetch_add(1, std::memory_order_relaxed);

        Response res;
        res.version(req_copy->version());
        res.keep_alive(req_copy->keep_alive());
        if (req_copy->target() == "/telemetry/data") {
            res.set(http::field::content_type, "application/json");
        }

        self->app_.handle(*ctx, *req_copy, res);
        res.result(to_http_status(ctx->status));
        res.prepare_payload();

        self->ctx_pool_.release();

        asio::post(self->socket_.get_executor(),
            [self, res = std::make_shared<Response>(std::move(res))]() {
                self->do_write(res);
            });
    });
}

void HttpSession::do_write(std::shared_ptr<Response> res) {
    auto self = shared_from_this();
    http::async_write(socket_, *res,
        [self, res](beast::error_code ec, std::size_t) {
            if (ec) {
                return;
            }
            if (!res->keep_alive()) {
                return self->do_close();
            }
            self->do_read();
        });
}

void HttpSession::do_close() {
    beast::error_code ec;
    socket_.shutdown(tcp::socket::shutdown_send, ec);
    socket_.close(ec);
}

Listener::Listener(asio::io_context& ioc,
                   tcp::endpoint endpoint,
                   App& app,
                   WorkerPool& workers,
                   contextPool& ctx_pool)
    : acceptor_(ioc),
      app_(app),
      workers_(workers),
      ctx_pool_(ctx_pool) {
    beast::error_code ec;
    acceptor_.open(endpoint.protocol(), ec);
    acceptor_.set_option(asio::socket_base::reuse_address(true), ec);
    acceptor_.bind(endpoint, ec);
    acceptor_.listen(asio::socket_base::max_listen_connections, ec);
}

void Listener::run() { do_accept(); }

void Listener::do_accept() {
    auto self = shared_from_this();
    acceptor_.async_accept(
        [self](beast::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::make_shared<HttpSession>(
                    std::move(socket),
                    self->app_,
                    self->workers_,
                    self->ctx_pool_,
                    self->request_id_
                )->run();
            }
            self->do_accept();
        });
}
