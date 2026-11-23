#include "gpu_api.h"
#include "gpu_resources.h"
__device__ unsigned int displayChannel(float x) {
    x=fmaxf(0,x);
    x=fminf(1.0f,(x*(2.51f*x+0.03f))/(x*(2.43f*x+0.59f)+0.14f));
    x=x<=0.0031308f?12.92f*x:1.055f*powf(x,1/2.4f)-0.055f;
    return static_cast<unsigned int>(fminf(255.0f,x*255+0.5f));
}
__global__ void toneMapDisplay(const Vec3* accumulation, uint32_t* display, int count,
                                int samples, float exposure) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if (i>=count) return;
    Vec3 c=accumulation[i]*(exposure/float(samples));
    display[i]=0xff000000u|displayChannel(c.x)|(displayChannel(c.y)<<8)|(displayChannel(c.z)<<16);
}

void launchToneMap(const Vec3* accumulation,uint32_t* display,int count,int samples,float exposure) {
    toneMapDisplay<<<(count+255)/256,256>>>(accumulation,display,count,samples,exposure);
    check(cudaGetLastError(),"Launch tone mapping");
}
