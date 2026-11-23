#pragma once
#include "water_model.h"
namespace tank {
__device__ inline bool belowSurface(const Vertex* grid,Vec3 p) {
    if (p.y<level-maxRise) return true;
    if (p.y>level+maxRise) return false;
    float x=fminf(float(nx),fmaxf(0,(p.x-x0)/dx)),z=fminf(float(nz),fmaxf(0,(p.z-z0)/dz));
    int ix=min(int(x),nx-1), iz=min(int(z),nz-1), i=iz*stride+ix;
    float u=x-ix,v=z-iz,b=grid[i+stride].y,c=grid[i+1].y;
    float h;
    if (u+v<=1) { float a=grid[i].y; h=a+u*(c-a)+v*(b-a); }
    else { float d=grid[i+stride+1].y; h=d+(1-u)*(b-d)+(1-v)*(c-d); }
    return p.y<h;
}
enum Medium { Air, Glass, Water };
__device__ inline Medium mediumAt(const Vertex* grid,Vec3 p) {
    if (p.x<left || p.x>right || p.z<back || p.z>front || p.y<bottom || p.y>=top) return Air;
    if (p.x<x0 || p.x>x1 || p.z<z0 || p.z>z1 || p.y<bottom+pane) return Glass;
    return belowSurface(grid,p)?Water:Air;
}
__device__ inline Medium otherMedium(MaterialType type,Medium from,const Vertex* grid,Vec3 p) {
    if (type==WATER_SURFACE) return from==Water?Air:Water;
    if (from!=Glass) return Glass;
    return type==GLASS_INNER && belowSurface(grid,p)?Water:Air;
}
__device__ inline float ior(Medium medium) { return medium==Water?1.333f:medium==Glass?1.50f:1.0f; }
__device__ inline float surfacePdf(float distance2,Vec3 geometric,Vec3 direction) {
    float projected=fmaxf(1e-9f,fabsf(dot(geometric,direction)));
    return distance2*fabsf(geometric.y)/((x1-x0)*(z1-z0)*projected);
}
__device__ inline Vec3 transmittance(Medium medium,float distance) {
    Vec3 sigma=medium==Water?Vec3(0.42f,0.115f,0.065f):Vec3(0.68f,0.13f,0.32f);
    return {expf(-sigma.x*distance),expf(-sigma.y*distance),expf(-sigma.z*distance)};
}
// Unpolarized Fresnel and Snell refraction with a face-forward interface normal.
__device__ inline float refract(Vec3 incident,Vec3 normal,float eta,Vec3& transmitted) {
    float ci=fminf(1.0f,fmaxf(0.0f,-dot(incident,normal)));
    float ct2=1-eta*eta*(1-ci*ci);
    if (ct2<=0) return 1;
    float ct=sqrtf(ct2);
    transmitted=unit(incident*eta+normal*(eta*ci-ct));
    float rs=(eta*ci-ct)/(eta*ci+ct),rp=(ci-eta*ct)/(ci+eta*ct);
    return 0.5f*(rs*rs+rp*rp);
}
} // namespace tank
