#pragma once
#include "scene_types.h"

// World-space projection keeps grain aligned across the room's separate wall quads.
__device__ inline Vec3 sampleMaterial(const Scene& scene,const Material& material,const Hit& hit,
                                      Vec3 direction,float pixelWidth) {
    if (material.texture==NoTexture) return Vec3(1);
    Vec3 n=hit.geometricNormal,p=hit.position*material.textureScale;
    float u,v;
    if (fabsf(n.y)>=fabsf(n.x) && fabsf(n.y)>=fabsf(n.z)) { u=p.x;v=p.z; }
    else if (fabsf(n.x)>fabsf(n.z)) { u=p.z;v=p.y; }
    else { u=p.x;v=p.y; }
    float incidence=fmaxf(0.15f,fabsf(dot(hit.normal,direction)));
    float lod=log2f(fmaxf(1.0f,pixelWidth*512*material.textureScale/incidence));
    float4 color=tex2DLod<float4>(scene.textures[material.texture],u,v,lod);
    return {color.x,color.y,color.z};
}
__device__ inline Vec3 samplePetal(const Scene& scene,const Hit& hit,Vec3 direction,float pixelWidth) {
    float incidence=fmaxf(0.15f,fabsf(dot(hit.normal,direction)));
    float footprint=pixelWidth*scene.petalTexelsPerMeter/incidence;
    float lod=log2f(fmaxf(1.0f,footprint));
    float4 texel=tex2DLod<float4>(scene.petalTexture,hit.petalUv.x,hit.petalUv.y,lod);
    return {texel.x,texel.y,texel.z};
}
