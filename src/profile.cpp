#include "profile.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <vector>

/**
 * @brief Global Telemetry Collector
 * In a real-world scenario, this might stream via ZeroMQ or write to a ring buffer 
 * for a Chrome Tracing (JSON) frontend to consume.
 */
void TelemetryHarvesterLoop(std::atomic<bool>& running) {
    std::ofstream log_file("telemetry_stream.json", std::ios::app);
    
    while (running) {
        // Collect data from the dual-buffer system in TelemetryHub
        std::string frame_data = TELEMETRY_FLUSH();
        
        if (frame_data.length() > 50) { // Only log if there's actual activity
            log_file << frame_data << std::endl;
        }

        // 60 FPS Collection Rate
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

/**
 * @brief Example Network Service
 * Demonstrates the use of NET_SCOPE to profile socket-level throughput.
 */
class MockNetworkService {
public:
    void SendPayload(const std::vector<uint8_t>& data) {
        PROFILE_SCOPE("NetworkService::SendPayload");
        
        size_t bytes_processed = 0;
        {
            // Track the specific I/O duration and byte count
            NET_SCOPE("Socket::Send", bytes_processed);
            
            // Simulating network latency and "work"
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            bytes_processed = data.size();
            
            // Update the hub with the results
            CallStack::TelemetryHub::instance().update_network(bytes_processed, 0, 5000000);
        }
    }
};

/**
 * @brief Logic Simulation
 * Simulates a standard game/app loop with nested profiling scopes.
 */
void AppLogic() {
    PROFILE_SCOPE("MainLoop");
    
    {
        PROFILE_SCOPE("PhysicsUpdate");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        PROBE_POINT("Physics_Step_Done"); // eBPF Hook point
    }

    {
        PROFILE_SCOPE("Rendering");
        GPU_SCOPE("ShadowPass"); // Vulkan/GPU breadcrumb
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }

    // Trigger Network activity
    MockNetworkService net;
    std::vector<uint8_t> packet(1024, 0xFF);
    net.SendPayload(packet);
}