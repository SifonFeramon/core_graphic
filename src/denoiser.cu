#include "denoiser.h"
#include "gpu_resources.h"
#include "intersection.cuh"
#include "material_sampling.cuh"
#include "playback_sampling.h"
// Edge-avoiding a-trous filtering in linear HDR. The accumulation is never modified.
// Based on Dammertz et al., HPG 2010: https://jo.dreggn.org/home/2010_atrous.pdf
struct DenoiseGuide {
    Vec3 position, normal;
    int material, primaryMaterial;
    Vec3 pigment{1};
};

__device__ bool lightMayCrossMotion(const Scene& scene, Vec3 position) {
    const Quad& light=scene.quads[scene.lightIndex];
    float height=light.origin.y-position.y;
    if (height<=EPS) return false;
    float near=fmaxf(0,(scene.motionLow.y-position.y)/height);
    float far=fminf(1,(scene.motionHigh.y-position.y)/height);
    if (near>far) return false;
    // Bound the cone from this surface point to the four corners of the ceiling light.
    Vec3 low=minimum(minimum(light.origin,light.origin+light.u),
                     minimum(light.origin+light.v,light.origin+light.u+light.v));
    Vec3 high=maximum(maximum(light.origin,light.origin+light.u),
                      maximum(light.origin+light.v,light.origin+light.u+light.v));
    low=minimum(position+(low-position)*near,position+(low-position)*far);
    high=maximum(position+(high-position)*near,position+(high-position)*far);
    return low.x<=scene.motionHigh.x && high.x>=scene.motionLow.x
        && low.z<=scene.motionHigh.z && high.z>=scene.motionLow.z;
}

__global__ void buildDenoiseGuides(Scene scene, Camera camera, DenoiseGuide* guides,
                                  int width, int height, uint8_t* historyMask) {
    int x=blockIdx.x*blockDim.x+threadIdx.x, y=blockIdx.y*blockDim.y+threadIdx.y;
    if (x>=width || y>=height) return;
    float u=(2*(x+0.5f)/width-1)*camera.aspect*camera.tanHalfFov;
    float v=(1-2*(y+0.5f)/height)*camera.tanHalfFov;
    Ray ray{camera.position,unit(camera.forward+camera.right*u+camera.up*v)};
    Ray primaryRay=ray;
    float primaryDistance=1e30f, pathLength=0;
    bool specular=false;
    tank::Medium medium=tank::mediumAt(scene.waterGrid,ray.origin);
    DenoiseGuide guide{}; guide.material=-1;guide.primaryMaterial=-1;
    // Follow ideal reflections so reflected silhouettes also stop the filter.
    for (int bounce=0;bounce<8;++bounce) {
        Hit hit;
        if (!intersect(scene,ray,hit)) break;
        pathLength+=hit.t;
        if (bounce==0) {
            guide.primaryMaterial=hit.material;
            primaryDistance=hit.t;
        }
        const Material& material=scene.materials[hit.material];
        if (tankInterface(material.type)) {
            specular=true;
            if (guide.material<0) {
                guide.position=hit.position;guide.normal=hit.normal;guide.material=hit.material;
            }
            auto interface=tank::interfaceAt(scene,material.type,medium,hit,ray.direction);
            bool reflect=interface.reflectance>=1;
            ray=tank::scatterRay(hit,ray.direction,interface,reflect);
            if (!reflect) medium=interface.next;
            continue;
        }
        if (material.type==THIN_GLASS) {
            // Once the petals settle, keep accumulating fixed cloche views unless
            // their reflected ray can see the moving aquarium.
            if (scene.petalsPlaying) specular=true;
            else if (!specular) {
                Vec3 r=ray.direction-hit.normal*(2*dot(ray.direction,hit.normal));
                specular=intersectsBounds({tank::left,tank::bottom,tank::back},{tank::right,tank::top,tank::front},
                                          {hit.position,r},{1/r.x,1/r.y,1/r.z},1e30f);
            }
            // Keep a guide for reflections even when the transmitted ray sees only sky.
            if (guide.material<0) {
                guide.position=hit.position;
                guide.normal=hit.normal;
                guide.material=hit.material;
            }
            ray.origin=hit.position+ray.direction*(2*EPS);
            continue;
        }
        if (material.type!=MIRROR) {
            guide.position=hit.position;guide.normal=hit.normal;
            guide.material=material.type==DIFFUSE?hit.material:-1;
            Vec3 pigment=sampleMaterial(scene,material,hit,ray.direction,pathLength*2*camera.tanHalfFov/height);
            guide.pigment=maximum(pigment,Vec3(0.08f));
            if (hit.petalUv.x>=0) {
                Vec3 color=samplePetal(scene,hit,ray.direction,pathLength*2*camera.tanHalfFov/height);
                guide.pigment=Vec3(fmaxf(0.15f,color.x));
            }
            break;
        }
        specular=true;
        ray={hit.position+hit.normal*(2*EPS),
             unit(ray.direction-hit.normal*(2*dot(ray.direction,hit.normal)))};
    }
    int index=y*width+x;
    guides[index]=guide;
    if (historyMask) {
        Vec3 direction=primaryRay.direction;
        Vec3 inverse{1.0f/direction.x,1.0f/direction.y,1.0f/direction.z};
        bool roseMayMove=scene.petalsPlaying && (intersectsBounds(scene.motionLow,scene.motionHigh,primaryRay,inverse,primaryDistance+0.01f)
                                                  || lightMayCrossMotion(scene,guide.position));
        bool stable=!specular && guide.material>=0 && !roseMayMove;
        bool luminousPetal=scene.petalsPlaying && guide.material>=0 && scene.materials[guide.material].emission.x>0;
        historyMask[index]=specular || luminousPetal?FineDetail:stable?StableSurface:CurrentFrame;
    }
}

struct HistoryPixel {
    Vec3 mean;
    int samples;
};

constexpr int PlaybackHistorySamples=32;


__global__ void mergePlaybackHistory(const Vec3* accumulation, const uint8_t* mask, HistoryPixel* history,
                                     Vec3* output,const DenoiseGuide* guides,int count,int width,int samples,bool reuse,bool update) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if (i>=count) return;
    Vec3 mean=accumulation[i]/float(samples);
    if (mask[i]!=StableSurface) {
        if (update) history[i].samples=0;
        output[i]=mean/guides[i].pigment;
        return;
    }
    // Cap the history so gradual indirect lighting changes can still come through.
    int actual=update?playbackSampleCount(mask,i,i%width,width,samples):samples;
    int retained=reuse?min(history[i].samples,PlaybackHistorySamples-actual):0;
    if (retained) mean=(mean*float(actual)+history[i].mean*float(retained))/float(actual+retained);
    if (update) history[i]={mean,actual+retained};
    output[i]=mean/guides[i].pigment;
}

__device__ float luminance(Vec3 c) { return dot(c,Vec3(0.2126f,0.7152f,0.0722f)); }

__global__ void filterAtrous(const Vec3* input, Vec3* output, const DenoiseGuide* guides,
                            int width, int height, int step, float colorSigma, const HistoryPixel* history) {
    int x=blockIdx.x*blockDim.x+threadIdx.x, y=blockIdx.y*blockDim.y+threadIdx.y;
    if (x>=width || y>=height) return;
    int index=y*width+x;
    DenoiseGuide center=guides[index];
    Vec3 color=input[index];
    // Leave sky, directly visible emission and unresolved mirror chains untouched.
    if (center.material<0) { output[index]=color;return; }
    if (history && history[index].samples>0)
        colorSigma=fmaxf(0.12f,2*rsqrtf(float(history[index].samples)));
    const float kernel[5]={1,4,6,4,1};
    Vec3 sum(0);float total=0;
    float centerLuma=luminance(color);
    for (int ky=-2;ky<=2;++ky) for (int kx=-2;kx<=2;++kx) {
        int nx=x+kx*step,ny=y+ky*step;
        if (nx<0 || nx>=width || ny<0 || ny>=height) continue;
        int neighbor=ny*width+nx;
        DenoiseGuide other=guides[neighbor];
        if (other.material!=center.material || other.primaryMaterial!=center.primaryMaterial) continue;
        float normalWeight=powf(fmaxf(0,dot(center.normal,other.normal)),32.0f);
        Vec3 delta=other.position-center.position;
        // Tangent-plane distance permits smoothing slanted walls, but rejects depth edges.
        float distance2=dot(delta,delta);
        float plane=fmaxf(fabsf(dot(delta,center.normal)),fabsf(dot(delta,other.normal)));
        float positionWeight=expf(-plane/(0.002f+0.05f*sqrtf(distance2)));
        float otherLuma=luminance(input[neighbor]);
        float colorWeight=expf(-fabsf(centerLuma-otherLuma)/
            (colorSigma*(0.1f+fmaxf(centerLuma,otherLuma))));
        float weight=kernel[kx+2]*kernel[ky+2]*normalWeight*positionWeight*colorWeight;
        sum+=input[neighbor]*weight;total+=weight;
    }
    output[index]=sum/total; // The center always contributes weight 36.
}

// Filter lighting separately from known material color, then restore texture detail.
template<bool Restore>
__global__ void scaleDenoiseRadiance(const Vec3* input,Vec3* output,const DenoiseGuide* guides,int count,float scale) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if (i<count) {
        Vec3 pigment=guides[i].pigment;
        output[i]=Restore?input[i]*pigment*scale:(input[i]/pigment)*scale;
    }
}

struct Denoiser::Impl {
    struct Playback {
        DeviceBuffer<uint8_t> mask;
        DeviceBuffer<HistoryPixel> history;
        explicit Playback(size_t count) : mask(count),history(count) {}
    };
    int width, height;
    DeviceBuffer<DenoiseGuide> guides;
    DeviceBuffer<Vec3> scratchA, scratchB;
    std::unique_ptr<Playback> playback;
    bool historyValid=false;
public:
    Impl(int w,int h) : width(w),height(h),guides(size_t(w)*h),
        scratchA(size_t(w)*h),scratchB(size_t(w)*h) {}

    bool preparePlayback() {
        if (playback) return false;
        playback=std::make_unique<Playback>(size_t(width)*height);
        return true;
    }

    void resetHistory() { historyValid=false; }
    const uint8_t* historyMask() const { return playback->mask.data; }

    void updateGuides(Scene scene,Camera camera) {
        dim3 block(16,8),grid((width+15)/16,(height+7)/8);
        buildDenoiseGuides<<<grid,block>>>(scene,camera,guides.data,width,height,playback?playback->mask.data:nullptr);
        check(cudaGetLastError(),"Build denoising guides");
    }

    // Both input and output use accumulated (summed) radiance, matching tone mapping.
    const Vec3* apply(const Vec3* accumulation,int samples,DenoiseMode mode=DenoiseMode::Spatial) {
        dim3 block(16,8),grid((width+15)/16,(height+7)/8);
        float colorSigma=fmaxf(0.12f,2.0f/std::sqrt(float(samples)));
        // Normalize once, filter, then restore the sum expected by existing consumers.
        int pixels=width*height,groups=(pixels+255)/256;
        const HistoryPixel* history=nullptr;
        bool update=mode==DenoiseMode::Playback;
        bool blend=update || (mode==DenoiseMode::Settling && historyValid && samples<PlaybackHistorySamples);
        if (blend) {
            history=playback->history.data;
            mergePlaybackHistory<<<groups,256>>>(accumulation,playback->mask.data,playback->history.data,
                                                 scratchB.data,guides.data,pixels,width,samples,historyValid,update);
            check(cudaGetLastError(),"Merge playback history");
        } else {
            scaleDenoiseRadiance<false><<<groups,256>>>(accumulation,scratchB.data,guides.data,pixels,1.0f/samples);
            check(cudaGetLastError(),"Normalize denoising input");
        }
        historyValid=blend;
        const Vec3* input=scratchB.data;
        for (int pass=0;pass<3;++pass) {
            Vec3* output=pass%2==0?scratchA.data:scratchB.data;
            filterAtrous<<<grid,block>>>(input,output,guides.data,width,height,1<<pass,colorSigma,history);
            check(cudaGetLastError(),"Launch a-trous denoising");
            input=output;
        }
        scaleDenoiseRadiance<true><<<groups,256>>>(scratchA.data,scratchB.data,guides.data,pixels,float(samples));
        check(cudaGetLastError(),"Restore denoised radiance scale");
        return scratchB.data;
    }
};

Denoiser::Denoiser(int w,int h):impl(std::make_unique<Impl>(w,h)) {}
Denoiser::~Denoiser()=default;
bool Denoiser::preparePlayback() { return impl->preparePlayback(); }
void Denoiser::resetHistory() { impl->resetHistory(); }
const uint8_t* Denoiser::historyMask() const { return impl->historyMask(); }
void Denoiser::updateGuides(Scene scene,Camera camera) { impl->updateGuides(scene,camera); }
const Vec3* Denoiser::apply(const Vec3* accumulation,int samples,DenoiseMode mode) { return impl->apply(accumulation,samples,mode); }
