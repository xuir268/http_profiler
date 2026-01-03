#pragma once
#include <functional>
#include <iostream>
#include <type_traits>

#include <boost/beast/http.hpp>
#include "context_pool.h"

namespace http = boost::beast::http;

using Request = http::request<http::string_body>;
using Response = http::response<http::string_body>;


#ifndef __host__
    #define __host__
#endif
#ifndef __device__
    #define __device__
#endif


template<typename T, typename... Args>
class StaticPipeline {
public:
    static inline void execute(Context& ctx) {
      bool success = (Stages::process(ctx) && ...);
        
        if (!success) {
            ctx.mark_failed(); 
        }
    }
};

namespace Middleware {
    struct Logger {
        static inline bool  PrintLn(ReqContext& ctx) {
            std::cout << "[Request ID: " << ctx.request_id << "] "
                      << ctx.method << " " << ctx.target << " "
                      << std::endl;
            return true;
        }
    };
};



using Middleware = StaticPipeline<
    struct ReqContext,
    Middleware::Logger
>;