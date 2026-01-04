#pragma once
#include "middleware.h" 

#include <boost/beast/http.hpp>

namespace http = boost::beast::http;
using Request = http::request<http::string_body>;
using Response = http::response<http::string_body>;
/**
 * 
 * * ARCHITECTURAL DECISION:
 * We use the 'MiddlewareStack' (a template-based StaticPipeline) inside the 
 * App's hot-path. By using a fold-expression internally, the compiler 
 * transforms the sequence of middleware calls into a flat, branchless 
 * execution block.
 */
class App {
public:
    /**
     * @brief Construct the App.
     * In this architecture, the App is a stateless coordinator.
     */
    App() = default;

    /**
     * @brief The System "Hot Path"
     * This method is the entry point for processed requests.
     * @param ctx The pre-allocated, cache-aligned request context.
     */
    inline void handle(ReqContext& ctx) const {
        
        Middleware::execute(ctx);
        if (ctx.status == HttpStatus::OK && !ctx.scratch.empty()) {
            return;
        }
        dispatch_to_handler(ctx);
    }

    inline void handle(ReqContext& ctx, const Request& req, Response& res) const {
        // 1. Sync Beast Request to ReqContext
        ctx.method = std::string(req.method_string());
        ctx.target = std::string(req.target());
        
        // 2. Run the optimized handle logic
        this->handle(ctx);

        // 3. Sync ReqContext result back to Beast Response
        res.body() = ctx.scratch;
    }

private:
    /**
     * @brief Static Dispatcher
     * Maps the validated request to the appropriate internal handler.
     */
    static inline void dispatch_to_handler(ReqContext& ctx) {
        // Optimized string comparison for routing
        if (ctx.target == "/health") {
            ctx.scratch = "SYSTEM_READY";
             ctx.status = HttpStatus::OK;
             return;
        }

        ctx.status = HttpStatus::NotFound;
        ctx.scratch = "NOT_FOUND";
    }
   
};
