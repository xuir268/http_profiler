#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <string_view>
#include "context_pool.h"
#include "profile.h" 

#ifndef __host__
    #define __host__
#endif
#ifndef __device__
    #define __device__
#endif

template <typename Context, typename... Stages>
class StaticPipeline {
public:
    static inline void execute(Context& ctx) {
        (void)(Stages::process(ctx) && ...);
    }
};

namespace Middlewares {
    
    /**
     * @brief Recursive JSON Builder
     * Creates a nested "Russian Doll" style JSON so the UI shows a vertical stack.
     */
    static inline std::string serialize_callstack() {
        auto& stack = CallStack::get_stack();
        
        if (stack.empty()) {
            return R"({"name": "idle", "value": 1000})";
        }

        std::string json = "{\"name\": \"request_root\", \"children\": [";
        std::vector<std::string> closers;
        closers.push_back("]}");

        for (size_t i = 0; i < stack.size(); ++i) {
            json += "{\"name\": \"" + std::string(stack[i]) + "\"";
            
            if (i == stack.size() - 1) {
                json += ", \"value\": 1000 }";
            } else {
                json += ", \"children\": [";
                closers.push_back("]}");
            }
        }

        for (auto it = closers.rbegin(); it != closers.rend(); ++it) {
            json += *it;
        }

        return json;
    }

    /**
     * @brief Direct UI Renderer
     * Injects the dynamic JSON into a pre-defined HTML template.
     */
    static inline std::string render_ui(const std::string& json_data) {
        // Optimization: In a production environment, this template would be 
        // pre-loaded into a string_view to avoid repeated string allocations.
        std::string html = R"(
<!DOCTYPE html>
<html>
<head>
    <script src="https://d3js.org/d3.v7.min.js"></script>
    <script src="https://cdn.tailwindcss.com"></script>
    <style>
        .node rect { stroke: #020617; stroke-width: 1px; transition: 0.2s; }
        .node rect:hover { filter: saturate(2) brightness(1.5); cursor: crosshair; }
        .node text { font-size: 10px; fill: rgba(255,255,255,0.9); font-family: 'JetBrains Mono', monospace; pointer-events: none; font-weight: 600; }
    </style>
</head>
<body class="bg-slate-950 text-slate-200 p-8">
    <div class="max-w-6xl mx-auto">
        <div class="flex items-center justify-between mb-6">
            <div>
                <h1 class="text-3xl font-black tracking-tighter text-emerald-400">PIPELINE_HEAT_MAP</h1>
                <p class="text-slate-500 text-xs font-mono uppercase tracking-widest">Monotonic Stack Depth Analysis</p>
            </div>
            <div class="text-right">
                <span class="px-2 py-1 rounded bg-emerald-500/10 border border-emerald-500/20 text-emerald-500 text-[10px] font-bold">LIVE_STREAM_ACTIVE</span>
            </div>
        </div>
        
        <div id="chart" class="bg-slate-900 rounded-lg border border-slate-800 p-1 shadow-2xl overflow-hidden"></div>
    </div>

    <script>
        const data = )" + json_data + R"(;
        const container = document.getElementById('chart');
        const width = container.offsetWidth;
        const height = 600;
        
        const svg = d3.select("#chart").append("svg")
            .attr("width", width)
            .attr("height", height)
            .attr("viewBox", [0, 0, width, height]);

        // Create a heat-based color scale (Emerald for shallow, Amber/Orange for deep)
        const color = d3.scaleSequential()
            .domain([0, 10]) 
            .interpolator(d3.interpolateRgbBasis(["#10b981", "#3b82f6", "#f59e0b", "#ef4444"]));

        const root = d3.hierarchy(data)
            .sum(d => d.value);

        // Icicle plot layout (Top-to-Bottom hierarchy)
        d3.partition()
            .size([width, height])
            .padding(1)
            (root);

        const nodes = svg.selectAll(".node")
            .data(root.descendants())
            .enter().append("g")
            .attr("class", "node")
            .attr("transform", d => `translate(${d.x0},${d.y0})`);

        nodes.append("rect")
            .attr("width", d => d.x1 - d.x0)
            .attr("height", d => d.y1 - d.y0)
            .attr("fill", d => color(d.depth));
        
        nodes.append("text")
            .attr("dx", 8)
            .attr("dy", 18)
            .text(d => (d.x1 - d.x0 > 50) ? d.data.name.toUpperCase() : "");

        nodes.append("text")
            .attr("dx", 8)
            .attr("dy", 32)
            .attr("class", "opacity-40 text-[8px]")
            .text(d => (d.x1 - d.x0 > 50) ? `DEPTH: ${d.depth}` : "");
    </script>
</body>
</html>)";
        return html;
    }

    struct Logger {
        static inline bool process(ReqContext& ctx) {
            CallStack::CallFrame frame{"Middleware::Logger"};
            std::cout << "[RID: " << ctx.request_id << "] " << ctx.target << std::endl;
            return true;
        }
    };

    struct Router {
        static inline bool process(ReqContext& ctx) {
            // Note: Router itself creates a CallFrame which will be visible in the trace
            CallStack::CallFrame frame{"Middleware::Router"};
            
            if (ctx.target == "/telemetry") {
                ctx.status = HttpStatus::OK;
                // Issue Fix: We generate the trace up to this exact point
                // and wrap it in the HTML visualization for direct browser streaming.
                ctx.scratch = render_ui(serialize_callstack());
                return false; 
            }
            return true;
        }
    };
}

using MiddlewareStack = StaticPipeline<ReqContext, Middlewares::Logger, Middlewares::Router>;

struct MiddlewareWrapper {
    static inline void execute(ReqContext& ctx) {
        MiddlewareStack::execute(ctx);
    }
};

using Middleware = MiddlewareWrapper;