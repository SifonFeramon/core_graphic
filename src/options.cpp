#include "app.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
int positiveInt(const std::string& s) {
    size_t end; long long n=std::stoll(s,&end);
    if (end!=s.size() || n<1 || n>std::numeric_limits<int>::max()) throw std::runtime_error("Expected a positive integer: "+s);
    return int(n);
}
Options parse(int argc,char** argv) {
    Options o;
    for (int i=1;i<argc;++i) {
        std::string arg=argv[i];
        if (arg=="--help" || arg=="-h") {
            std::cout<<"CUDA room path tracer\nOptions: --width N --height N --spp 256 --bounces N\n"
                       "         --exposure 1.0 --seed 42 --output room.bmp (or .ppm)\n"
                       "         --no-denoise disables the default GPU denoiser.\n"
                       "Windows: live viewer by default. --offline saves an image instead.\n"
                       "         --frames N closes the viewer after N frames (smoke test).\n";
            std::cout<<"         --profile reports live GPU stages and CPU frame latency.\n";
            std::cout<<"         Defaults: 1280x720 / 8 bounces live, 3840x2160 / 12 bounces offline.\n";
            std::cout<<"         --live-spp 2 sets average samples per live frame (up to 8).\n";
            std::cout<<"         --no-bvh uses linear intersections for validation and benchmarking.\n";
            std::cout<<"         --scene-time SECONDS holds the water and rose at the given time.\n";
            std::exit(0);
        }
        if (arg=="--offline") { o.offline=true; continue; }
        if (arg=="--no-denoise") { o.denoise=false; continue; }
        if (arg=="--profile") { o.profile=true; continue; }
        if (arg=="--no-bvh") { o.useBvh=false; continue; }
        if (i+1>=argc) throw std::runtime_error("Missing value for "+arg);
        std::string value=argv[++i];
        if (arg=="--width") o.width=positiveInt(value);
        else if (arg=="--height") o.height=positiveInt(value);
        else if (arg=="--spp") o.spp=positiveInt(value);
        else if (arg=="--bounces") o.bounces=positiveInt(value);
        else if (arg=="--frames") o.frames=positiveInt(value);
        else if (arg=="--live-spp") o.liveSpp=positiveInt(value);
        else if (arg=="--seed") o.seed=uint32_t(positiveInt(value));
        else if (arg=="--output") o.output=value;
        else if (arg=="--scene-time") {
            size_t end;
            float seconds=std::stof(value,&end);
            if (end!=value.size() || !std::isfinite(seconds) || seconds<0)
                throw std::runtime_error("Scene time must be finite and nonnegative");
            o.sceneTime=seconds;
        }
        else if (arg=="--exposure") {
            size_t end; o.exposure=std::stof(value,&end);
            if (end!=value.size() || !std::isfinite(o.exposure) || o.exposure<=0) throw std::runtime_error("Exposure must be finite and positive");
        } else throw std::runtime_error("Unknown option: "+arg);
    }
    bool live=false;
#ifdef _WIN32
    live=!o.offline;
#endif
    if (!o.width) o.width=live?1280:3840;
    if (!o.height) o.height=live?720:2160;
    if (!o.bounces) o.bounces=live?8:12;
    if (o.width>16384 || o.height>16384 || o.bounces>128) throw std::runtime_error("Limits: width/height <= 16384, bounces <= 128");
    if (o.liveSpp>8) throw std::runtime_error("Limits: live-spp <= 8");
    if (o.output.size()<4 || (o.output.substr(o.output.size()-4)!=".bmp" && o.output.substr(o.output.size()-4)!=".ppm"))
        throw std::runtime_error("Output extension must be .bmp or .ppm");
    return o;
}
