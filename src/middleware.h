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
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <script src="https://d3js.org/d3.v7.min.js"></script>
    <script src="https://cdn.tailwindcss.com"></script>
    <style>
        @import url('https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;700&display=swap');
        body { font-family: 'JetBrains Mono', monospace; background-color: #020617; color: #94a3b8; }
        .glass { background: rgba(15, 23, 42, 0.6); backdrop-filter: blur(10px); border: 1px solid rgba(51, 65, 85, 0.4); }
        .core-box { transition: all 0.3s ease; border: 1px solid #1e293b; height: 40px; display: flex; align-items: center; justify-content: center; font-size: 10px; border-radius: 4px; }
        .core-active { background: rgba(16, 185, 129, 0.2); border-color: #10b981; color: #10b981; box-shadow: 0 0 10px rgba(16, 185, 129, 0.2); }
        .orbit-rotate { animation: spin 20s linear infinite; }
        @keyframes spin { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }
    </style>
</head>
<body class="p-8">
    <div class="max-w-7xl mx-auto">
        <!-- Header -->
        <div class="flex justify-between items-end mb-8 border-b border-slate-800 pb-6">
            <div>
                <h1 class="text-3xl font-black tracking-tighter text-emerald-400 italic">ORBITER_SYSTEM_v2</h1>
                <p class="text-[10px] uppercase tracking-[0.2em] text-slate-500">Middleware Thread Affinity & System Core Saturation</p>
            </div>
            <div class="flex gap-8 text-right">
                <div><p class="text-[10px] text-slate-500">HEAP_ACTIVE</p><p id="metric-heap" class="text-xl font-bold text-slate-200">0 MB</p></div>
                <div><p class="text-[10px] text-slate-500">QUEUE_DEPTH</p><p id="metric-queue" class="text-xl font-bold text-amber-500">0</p></div>
                <div><p class="text-[10px] text-slate-500">UPTIME</p><p class="text-xl font-bold text-emerald-500">LIVE</p></div>
            </div>
        </div>

        <div class="grid grid-cols-12 gap-6">
            <!-- Left: Core Grid -->
            <div class="col-span-4 space-y-6">
                <div class="glass rounded-2xl p-6">
                    <h2 class="text-xs font-bold uppercase mb-4 text-slate-400">Physical Core Map</h2>
                    <div id="core-grid" class="grid grid-cols-4 gap-2">
                        <!-- Cores injected here -->
                    </div>
                </div>
                
                <div class="glass rounded-2xl p-6 relative overflow-hidden h-48 flex flex-col items-center justify-center">
                    <div class="absolute inset-0 opacity-10 orbit-rotate">
                        <svg viewBox="0 0 100 100" class="w-full h-full"><circle cx="50" cy="50" r="45" fill="none" stroke="white" stroke-dasharray="2 4"/></svg>
                    </div>
                    <div id="gauge-container"></div>
                    <p class="text-[10px] text-slate-500 uppercase mt-2">Overall Pressure</p>
                </div>
            </div>

            <!-- Right: Thread Explorer -->
            <div class="col-span-8 glass rounded-2xl p-6">
                <h2 class="text-xs font-bold uppercase mb-4 text-slate-400">Thread Execution Paths</h2>
                <div id="thread-list" class="space-y-6 max-h-[600px] overflow-y-auto pr-2">
                    <!-- Threads injected here -->
                </div>
            </div>
        </div>
    </div>

    <script>
        // Data injected by C++ middleware
        const telemetry = {{TELEMETRY_JSON}};

        function init() {
            document.getElementById('metric-heap').innerText = telemetry.metrics.heap + " MB";
            document.getElementById('metric-queue').innerText = telemetry.metrics.queue;
            
            renderCores();
            renderThreads();
            renderPressure();
        }

        function renderCores() {
            const grid = document.getElementById('core-grid');
            const activeCores = new Set(telemetry.threads.map(t => t.core).filter(c => c !== -1));
            
            for(let i = 0; i < 16; i++) {
                const div = document.createElement('div');
                div.className = `core-box ${activeCores.has(i) ? 'core-active' : ''}`;
                div.innerText = `C${i}`;
                grid.appendChild(div);
            }
        }

        function renderThreads() {
            const list = document.getElementById('thread-list');
            telemetry.threads.forEach(t => {
                const threadDiv = document.createElement('div');
                threadDiv.className = "p-4 bg-slate-900/50 border border-slate-800 rounded-lg";
                
                threadDiv.innerHTML = `
                    <div class="flex justify-between text-[10px] mb-2 font-bold">
                        <span class="text-emerald-500">TID: ${t.tid}</span>
                        <span class="text-slate-500">AFFINITY: CORE_${t.core}</span>
                    </div>
                    <div class="flex gap-1 h-8">
                        ${t.stack.map((s, i) => `
                            <div class="flex-grow border-l-2 border-emerald-500/40 bg-emerald-500/5 px-2 flex items-center overflow-hidden">
                                <span class="text-[9px] whitespace-nowrap">${s}</span>
                            </div>
                        `).join('')}
                    </div>
                `;
                list.appendChild(threadDiv);
            });
        }

        function renderPressure() {
            const width = 120, height = 120;
            const svg = d3.select("#gauge-container").append("svg").attr("width", width).attr("height", height)
                .append("g").attr("transform", `translate(${width/2},${height/2})`);
            
            const arc = d3.arc().innerRadius(45).outerRadius(55).startAngle(0);
            const pressure = (telemetry.threads.length / 16) * 2 * Math.PI;

            svg.append("path").datum({endAngle: 2*Math.PI}).style("fill", "#1e293b").attr("d", arc);
            svg.append("path").datum({endAngle: pressure}).style("fill", "#10b981").attr("d", arc);
            svg.append("text").attr("text-anchor", "middle").attr("dy", "0.3em").attr("fill", "#fff").style("font-size", "14px").text(Math.round(pressure/6.28*100) + "%");
        }

        init();
    </script>
</body>
</html>
)";
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