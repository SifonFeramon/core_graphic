#include "app.h"
#include "room_scene.h"
#include "gpu_api.h"
#include "denoiser.h"
#include <iomanip>
#include <iostream>
int main(int argc,char** argv) {
    try {
        Options o=parse(argc,argv);
        cudaDeviceProp properties{};
        check(cudaGetDeviceProperties(&properties,0),"Find CUDA GPU");
        check(cudaSetDevice(0),"Select GPU");
        std::cout<<"GPU: "<<properties.name<<"\n";
        RoomScene room(o.useBvh);
        if (o.sceneTime) room.setTime(*o.sceneTime);
#ifdef _WIN32
        if (!o.offline) {
            std::cout<<o.width<<'x'<<o.height<<", "<<o.liveSpp<<" samples/pixel per frame, "<<o.bounces<<" bounces\n";
            return runLiveViewer(room,o);
        }
#else
        if (o.frames) throw std::runtime_error("The live viewer is Windows-only");
#endif
        std::cout<<o.width<<'x'<<o.height<<", "<<o.spp<<" samples/pixel, "<<o.bounces<<" bounces\n";
        Scene scene=room.view;
        size_t count=size_t(o.width)*o.height;
        DeviceBuffer<Vec3> accumulation(count);
        check(cudaMemset(accumulation.data,0,count*sizeof(Vec3)),"Clear accumulation");
        Camera camera=makeCamera({0,1.65f,2.8f},0,RoomCameraPitch,float(o.width)/o.height);
        std::unique_ptr<Denoiser> denoiser;
        if (o.denoise) {
            denoiser=std::make_unique<Denoiser>(o.width,o.height);
            denoiser->updateGuides(scene,camera);
        }
        cudaEvent_t start,stop;
        check(cudaEventCreate(&start),"Create timer"); check(cudaEventCreate(&stop),"Create timer");
        check(cudaEventRecord(start),"Start timer");
        // Short launches keep the desktop responsive and reduce watchdog timeout risk.
        for (int first=0;first<o.spp;) {
            int batch=std::min(8,o.spp-first);
            launchRender(scene,camera,accumulation.data,o.width,o.height,first,batch,o.bounces,o.seed);
            check(cudaGetLastError(),"Launch render kernel");
            check(cudaDeviceSynchronize(),"Render batch");
            first+=batch;
            std::cout<<"\rSamples: "<<first<<'/'<<o.spp<<std::flush;
        }
        check(cudaEventRecord(stop),"Stop timer"); check(cudaEventSynchronize(stop),"Wait for timer");
        float ms=0; check(cudaEventElapsedTime(&ms,start,stop),"Read timer");
        cudaEventDestroy(start);cudaEventDestroy(stop);
        std::vector<Vec3> pixels(count);
        const Vec3* result=denoiser?denoiser->apply(accumulation.data,o.spp):accumulation.data;
        check(cudaMemcpy(pixels.data(),result,count*sizeof(Vec3),cudaMemcpyDeviceToHost),"Download image");
        save(o,pixels);
        std::cout<<"\nDenoising: "<<(o.denoise?"on (GPU a-trous)":"off");
        std::cout<<"\nSaved "<<o.output<<" in "<<std::fixed<<std::setprecision(2)<<ms/1000<<" seconds (render).\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<"Error: "<<e.what()<<'\n';return 1; }
}
