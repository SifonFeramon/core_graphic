#include "gpu_api.h"
#include "gpu_resources.h"
#include "intersection.cuh"
#include "material_sampling.cuh"
#include "playback_sampling.h"
// An independent stream for every (pixel, sample), so batching does not change the image.
struct RNG {
    uint32_t state;
    __device__ explicit RNG(uint32_t pixel, uint32_t sample, uint32_t seed) {
        state = pixel * 747796405u + sample * 2891336453u + seed * 277803737u;
        state ^= state >> 16; state *= 2246822519u; state ^= state >> 13;
        if (!state) state = 1;
    }
    __device__ float next() {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return (float(state >> 8) + 0.5f) * (1.0f/16777216.0f);
    }
};

__device__ Vec3 cosineDirection(Vec3 n, RNG& rng) {
    float r=sqrtf(rng.next()), phi=2*PI*rng.next();
    Vec3 tangent=unit(cross(fabsf(n.y)<0.99f?Vec3(0,1,0):Vec3(1,0,0),n));
    Vec3 bitangent=cross(n,tangent);
    return unit(tangent*(r*cosf(phi))+bitangent*(r*sinf(phi))+n*sqrtf(fmaxf(0,1-r*r)));
}
__device__ float powerHeuristic(float a, float b) { return a*a/(a*a+b*b); }

// Sample a point on the moving surface, then follow Snell's law to the area light.
// The surface and cosine samplers are combined in solid angle at the submerged hit.
__device__ Vec3 sampleUnderwaterLight(const Scene& scene,const Hit& bedHit,float u,float v,bool continuation) {
    float x=u*tank::nx,z=v*tank::nz;
    int ix=min(int(x),tank::nx-1),iz=min(int(z),tank::nz-1);
    x-=ix;z-=iz;
    bool second=x+z>1;
    Triangle tri=tank::face(2*(iz*tank::nx+ix)+int(second),scene.waterGrid,0);
    float b=second?1-x:z,c=second?x+z-1:x;
    Vec3 surface=tri.origin+tri.edgeU*b+tri.edgeV*c,delta=surface-bedHit.position;
    float distance2=dot(delta,delta),distance=sqrtf(distance2);
    if (distance<=2*EPS) return {};
    Vec3 wi=delta/distance, geometric=unit(cross(tri.edgeU,tri.edgeV));
    float cosBed=dot(bedHit.normal,wi);
    if (cosBed<=0 || dot(bedHit.geometricNormal,wi)<=0 || dot(geometric,wi)<=0) return {};
    Hit h=triangleHit(tri,{surface,wi},0,b,c);
    auto boundary=tank::interfaceAt(scene,WATER_SURFACE,tank::Water,h,wi);
    if (boundary.reflectance>=1) return {};
    Ray out=tank::scatterRay(h,wi,boundary,false);
    const Quad& lamp=scene.quads[scene.lightIndex];
    float cosine=dot(lamp.normal,out.direction);
    if (cosine>=-1e-7f) return {};
    float t=dot(lamp.origin-out.origin,lamp.normal)/cosine;
    if (t<=EPS) return {};
    Vec3 d=out.origin+out.direction*t-lamp.origin;
    float lu=dot(d,lamp.u)/dot(lamp.u,lamp.u),lv=dot(d,lamp.v)/dot(lamp.v,lamp.v);
    if (lu<0 || lu>1 || lv<0 || lv>1) return {};
    Hit obstruction;
    float visibility=1;
    Vec3 start=bedHit.position+bedHit.geometricNormal*(2*EPS);
    Vec3 toSurface=surface-start;
    float length=sqrtf(dot(toSurface,toSurface));
    if (length<=2*EPS || intersect(scene,{start,toSurface/length},obstruction,length-2*EPS,&visibility)
        || intersect(scene,out,obstruction,t-2*EPS,&visibility)) return {};
    float pdf=tank::surfacePdf(distance2,geometric,wi), pass=1-boundary.reflectance;
    float weight=continuation?powerHeuristic(pdf,cosBed/PI*pass):1;
    return scene.materials[lamp.material].emission*tank::transmittance(tank::Water,distance)
           *(weight*visibility*cosBed*pass*boundary.eta*boundary.eta/(PI*pdf));
}
__device__ Vec3 sky(Vec3 d) {
    float t=0.5f*(d.y+1);
    return Vec3(0.65f,0.72f,0.85f)*(1-t)+Vec3(0.22f,0.46f,0.9f)*t;
}

__device__ Vec3 glowAlong(const Glow& glow, Ray ray, float maxT) {
    if (glow.radius<=0) return {};
    Vec3 delta=glow.center-ray.origin;
    float along=dot(delta,ray.direction);
    float radius2=glow.radius*glow.radius;
    float chord2=radius2-dot(delta,delta)+along*along;
    if (chord2<=0) return {};
    float halfChord=sqrtf(chord2);
    float start=fmaxf(-halfChord,-along);
    float end=fminf(halfChord,maxT-along);
    if (start>=end) return {};
    // Integrate a density of 1-r^2/R^2 over the visible part of the glow.
    float density=(end-start)*(chord2-(start*start+start*end+end*end)/3)/radius2;
    return glow.emission*density;
}

__device__ Vec3 trace(const Scene& scene, Ray ray, RNG& rng, int maxBounces, float pixelAngle) {
    Vec3 radiance(0), throughput(1), previousPosition;
    float previousPdf=0, pathLength=0, emitterWeight=1; bool previousDelta=true;
    const Quad& light=scene.quads[scene.lightIndex];
    tank::Medium medium=tank::mediumAt(scene.waterGrid,ray.origin);
    for (int bounce=0,interfaces=0;bounce<maxBounces;) {
        Hit h;
        bool found=intersect(scene,ray,h);
        if (medium!=tank::Air && found) throughput=throughput*tank::transmittance(medium,h.t);
        radiance+=throughput*glowAlong(scene.glow,ray,h.t);
        if (!found) { radiance+=throughput*sky(ray.direction); break; }
        pathLength+=h.t;
        Material material=scene.materials[h.material];
        if (tankInterface(material.type)) {
            auto interface=tank::interfaceAt(scene,material.type,medium,h,ray.direction);
            bool reflect=interface.reflectance>=1 || rng.next()<interface.reflectance;
            emitterWeight=1;
            if (!reflect && !previousDelta && medium==tank::Water && material.type==WATER_SURFACE) {
                Vec3 segment=h.position-previousPosition;
                float pdf=tank::surfacePdf(dot(segment,segment),h.geometricNormal,ray.direction);
                emitterWeight=powerHeuristic(previousPdf*(1-interface.reflectance),pdf);
            }
            ray=tank::scatterRay(h,ray.direction,interface,reflect);
            if (reflect) ++bounce;
            else { throughput=throughput*(interface.eta*interface.eta);medium=interface.next; }
            previousDelta=true;
            if (++interfaces>=32) break;
            continue;
        }
        if (material.type==THIN_GLASS && rng.next()>=glassReflectance(h.normal,ray.direction)) {
            // Straight transmission leaves the preceding scattering event's MIS weight intact.
            ray.origin=h.position+ray.direction*(2*EPS);
            continue;
        }
        if (material.type==EMISSIVE) {
            if (h.quad==scene.lightIndex && dot(light.normal,-ray.direction)>0) {
                float weight=emitterWeight;
                if (!previousDelta) {
                    Vec3 d=h.position-previousPosition;
                    float cosine=dot(light.normal,-ray.direction);
                    float lightPdf=dot(d,d)/(light.area*cosine);
                    weight=powerHeuristic(previousPdf,lightPdf);
                }
                radiance+=throughput*material.emission*weight;
            }
            break;
        }
        emitterWeight=1;
        if (material.type==DIFFUSE) {
            material.color=material.color*sampleMaterial(scene,material,h,ray.direction,pathLength*pixelAngle);
            if (h.petalUv.x>=0) {
                Vec3 pigment=samplePetal(scene,h,ray.direction,pathLength*pixelAngle);
                material.color=material.color*pigment;
                // Let the original pink glow pick up the darker veins and paler rim.
                material.emission=material.emission*Vec3(pigment.x,pigment.x,0.4f*pigment.x+4.5f*pigment.z);
            }
            radiance+=throughput*material.emission;
            float lightU=rng.next(),lightV=rng.next();
            if (medium==tank::Water)
                radiance+=throughput*material.color*sampleUnderwaterLight(scene,h,lightU,lightV,bounce+1<maxBounces);
            // In air the direct connection is the straight segment to the ceiling light.
            Vec3 target=light.origin+light.u*lightU+light.v*lightV;
            Vec3 delta=target-h.position;
            float distance2=dot(delta,delta), distance=sqrtf(distance2);
            Vec3 wi=delta/distance;
            float surfaceCos=fmaxf(0,dot(h.normal,wi));
            float lightCos=fmaxf(0,dot(light.normal,-wi));
            if (medium!=tank::Water && surfaceCos>0 && lightCos>0 && dot(h.geometricNormal,wi)>0) {
                Hit obstruction;
                Vec3 shadowOrigin=h.position+h.geometricNormal*EPS*2;
                Vec3 shadowDelta=target-shadowOrigin;
                float shadowDistance=sqrtf(dot(shadowDelta,shadowDelta));
                float transmission=1;
                if (!intersect(scene,{shadowOrigin,shadowDelta/shadowDistance},obstruction,
                               shadowDistance-EPS*2,&transmission)) {
                    float lightPdf=distance2/(light.area*lightCos), bsdfPdf=surfaceCos/PI;
                    // At the final bounce there is no competing continuation sample.
                    float weight=bounce+1==maxBounces?1:powerHeuristic(lightPdf,bsdfPdf);
                    radiance+=throughput*material.color*scene.materials[light.material].emission
                        *(transmission*surfaceCos*weight/(PI*lightPdf));
                }
            }
            ray.direction=cosineDirection(h.normal,rng);
            if (dot(h.geometricNormal,ray.direction)<=0) break;
            previousPdf=fmaxf(0,dot(h.normal,ray.direction))/PI;
            previousDelta=false;
        } else {
            Vec3 reflected=ray.direction-h.normal*(2*dot(ray.direction,h.normal));
            if (dot(reflected,h.geometricNormal)<=0)
                reflected=ray.direction-h.geometricNormal*(2*dot(ray.direction,h.geometricNormal));
            ray.direction=unit(reflected);
            previousDelta=true;
        }
        throughput=throughput*material.color;
        previousPosition=h.position;
        ray.origin=h.position+h.geometricNormal*EPS*2;
        if (bounce>=3) {
            float survival=fminf(0.95f,largest(throughput));
            if (rng.next()>=survival) break;
            throughput=throughput/survival;
        }
        ++bounce;
    }
    return radiance;
}

// All trace launches use 16x8 threads. Bound register use across the two traversal paths.
__global__ void __launch_bounds__(128,6) render(Scene scene, Camera camera, Vec3* accumulation, int width, int height,
                       int firstSample, int sampleCount, int bounces, uint32_t seed,
                       const uint8_t* historyMask, uint32_t sampleOffset) {
    int x=blockIdx.x*blockDim.x+threadIdx.x, y=blockIdx.y*blockDim.y+threadIdx.y;
    if (x>=width || y>=height) return;
    int index=y*width+x;
    uint32_t offset=historyMask && historyMask[index]==StableSurface?sampleOffset:0;
    int actual=playbackSampleCount(historyMask,index,x,width,sampleCount);
    Vec3 sum(0);
    for (int s=firstSample;s<firstSample+actual;++s) {
        RNG rng(uint32_t(index),uint32_t(s)+offset,seed);
        float u=(2*(x+rng.next())/width-1)*camera.aspect*camera.tanHalfFov;
        float v=(1-2*(y+rng.next())/height)*camera.tanHalfFov;
        Ray ray{camera.position,unit(camera.forward+camera.right*u+camera.up*v)};
        sum+=trace(scene,ray,rng,bounces,2*camera.tanHalfFov/height);
    }
    if (actual!=sampleCount) sum=sum*(float(sampleCount)/actual);
    accumulation[index]+=sum;
}

void launchRender(Scene scene,Camera camera,Vec3* accumulation,int width,int height,
                  int firstSample,int sampleCount,int bounces,uint32_t seed,
                  const uint8_t* historyMask,uint32_t sampleOffset) {
    dim3 block(16,8),grid((width+15)/16,(height+7)/8);
    render<<<grid,block>>>(scene,camera,accumulation,width,height,firstSample,sampleCount,bounces,seed,historyMask,sampleOffset);
    check(cudaGetLastError(),"Launch path tracing");
}
