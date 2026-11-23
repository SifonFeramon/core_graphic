#pragma once
#include "gpu_resources.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
// CUDA events measure stages on the same stream as the renderer. Profiling is opt-in.
class FrameProfiler {
    bool enabled;
    cudaEvent_t events[6]{};
    int frames=0;
    struct Measurement { float stages[5]; double frame, present, copyWait, interval; };
    std::vector<Measurement> measurements;
public:
    explicit FrameProfiler(bool active) : enabled(active) {
        if (enabled) for (auto& event : events) check(cudaEventCreate(&event),"Create profiling event");
    }
    ~FrameProfiler() { for (auto event : events) if (event) cudaEventDestroy(event); }
    void mark(int stage) {
        if (enabled) check(cudaEventRecord(events[stage]),"Record profiling stage");
    }
    void finish(double frameMs,double presentMs,double copyWaitMs,double intervalMs) {
        if (!enabled) return;
        // Exclude startup, allocation, and the first five frames from steady-state results.
        if (++frames<=5) return;
        check(cudaEventSynchronize(events[5]),"Wait for profiling events");
        Measurement m{};m.frame=frameMs;m.present=presentMs;m.copyWait=copyWaitMs;m.interval=intervalMs;
        for (int i=0;i<5;++i)
            check(cudaEventElapsedTime(&m.stages[i],events[i],events[i+1]),"Read profiling stage");
        measurements.push_back(m);
    }
    void report() const {
        if (!enabled) return;
        if (measurements.empty()) { std::cout<<"Profiling needs more than 5 frames.\n";return; }
        double stages[5]{},frame=0,present=0,copyWait=0,interval=0;
        std::vector<double> latencies;
        for (const auto& m : measurements) {
            for (int i=0;i<5;++i) stages[i]+=m.stages[i];
            frame+=m.frame;present+=m.present;copyWait+=m.copyWait;interval+=m.interval;latencies.push_back(m.frame);
        }
        double n=double(measurements.size());
        std::sort(latencies.begin(),latencies.end());
        const char* labels[]={"Guides GPU", "Trace GPU", "Denoise GPU", "Tone map GPU", "Display transfer timeline"};
        std::cout<<"\nProfile: "<<measurements.size()<<" frames (5 warmup frames excluded)\n"
                 <<std::fixed<<std::setprecision(3);
        for (int i=0;i<5;++i) std::cout<<labels[i]<<": "<<stages[i]/n<<" ms/frame\n";
        std::cout<<"Present CPU: "<<present/n<<" ms/frame\n"
                 <<"Display transfer CPU (includes GPU wait): "<<copyWait/n<<" ms/frame\n"
                 <<"Frame work CPU: "<<frame/n<<" ms/frame; "<<1000*n/frame<<" work FPS\n"
                 <<"Frame work p50/p95: "<<latencies[latencies.size()/2]<<" / "
                 <<latencies[size_t((latencies.size()-1)*0.95)]<<" ms\n"
                 <<"Viewer frame cadence (including limiter): "<<1000*n/interval<<" FPS\n"
                 <<"Frame work excludes the 16 ms frame limiter.\n";
    }
};
