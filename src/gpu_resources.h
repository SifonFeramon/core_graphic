#pragma once
#include "scene_types.h"
#include <stdexcept>
#include <string>
#include <vector>
inline void check(cudaError_t result, const char* operation) {
    if (result!=cudaSuccess) throw std::runtime_error(std::string(operation)+": "+cudaGetErrorString(result));
}

// tools/embed_textures.py supplies these host-side mip levels in RGBA byte order.
extern const int petalTextureSize;
extern const std::uint32_t petalPixels[];
extern const std::uint32_t oakPixels[], plasterPixels[];
struct PetalTexture {
    cudaMipmappedArray_t array=nullptr;
    cudaTextureObject_t handle=0;

    PetalTexture()=default;
    PetalTexture(const PetalTexture&)=delete;
    PetalTexture& operator=(const PetalTexture&)=delete;
    void upload(const std::uint32_t* source=petalPixels,bool repeat=false) {
        int levels=1;
        for (int size=petalTextureSize;size>1;size/=2) ++levels;
        auto format=cudaCreateChannelDesc<uchar4>();
        check(cudaMallocMipmappedArray(&array,&format,make_cudaExtent(petalTextureSize,petalTextureSize,0),levels),
              "Allocate petal texture");
        const auto* pixels=source;
        int size=petalTextureSize;
        for (int i=0;i<levels;++i) {
            cudaArray_t level;
            check(cudaGetMipmappedArrayLevel(&level,array,i),"Get petal mip level");
            size_t pitch=size*sizeof(*pixels);
            check(cudaMemcpy2DToArray(level,0,0,pixels,pitch,pitch,size,cudaMemcpyHostToDevice),"Upload petal mip level");
            pixels+=size*size;
            size/=2;
        }
        cudaResourceDesc resource{};
        resource.resType=cudaResourceTypeMipmappedArray;
        resource.res.mipmap.mipmap=array;
        cudaTextureDesc sampler{};
        sampler.addressMode[0]=sampler.addressMode[1]=repeat?cudaAddressModeWrap:cudaAddressModeClamp;
        sampler.normalizedCoords=1;
        sampler.filterMode=sampler.mipmapFilterMode=cudaFilterModeLinear;
        sampler.readMode=cudaReadModeNormalizedFloat;
        sampler.sRGB=1;
        sampler.maxMipmapLevelClamp=float(levels-1);
        check(cudaCreateTextureObject(&handle,&resource,&sampler,nullptr),"Create petal sampler");
    }
    ~PetalTexture() {
        if (handle) cudaDestroyTextureObject(handle);
        if (array) cudaFreeMipmappedArray(array);
    }
};

template<class T> struct DeviceBuffer {
    T* data=nullptr;
    explicit DeviceBuffer(size_t count) { check(cudaMalloc(reinterpret_cast<void**>(&data),count*sizeof(T)),"cudaMalloc"); }
    ~DeviceBuffer() { if (data) cudaFree(data); }
    DeviceBuffer(const DeviceBuffer&)=delete;
    DeviceBuffer& operator=(const DeviceBuffer&)=delete;
    void upload(const std::vector<T>& values, size_t first=0) {
        check(cudaMemcpy(data+first,values.data()+first,(values.size()-first)*sizeof(T),cudaMemcpyHostToDevice),"upload scene");
    }
};
