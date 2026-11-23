#pragma once
#include "geometry.h"
namespace tank {
constexpr float left=-1.91f, right=-0.59f, back=-2.24f, front=-1.06f;
constexpr float bottom=0.658f, top=1.44f, pane=0.012f;
constexpr float bed=bottom+pane+0.048f, level=1.25f;
constexpr float x0=left+pane, x1=right-pane, z0=back+pane, z1=front-pane;
constexpr int nx=72, nz=60, stride=nx+1, vertices=stride*(nz+1), faces=2*nx*nz;
constexpr float dx=(x1-x0)/nx, dz=(z1-z0)/nz;
struct Mode { int x,z; float amplitude,phase; };
constexpr Mode modes[]={{1,0,.016f,.4f},{0,1,.011f,1.1f},{2,1,.008f,-.65f},
    {3,2,.005f,.2f},{5,1,.003f,1.3f},{2,5,.0028f,-.4f},{7,3,.0021f,.9f},{4,7,.0018f,-.85f},
    {9,2,.0012f,2.2f},{3,11,.0010f,-1.4f},{12,5,.0008f,.75f},{8,13,.00065f,2.8f},
    {15,4,.0005f,-2.1f},{5,16,.00045f,.32f},{18,9,.00035f,1.8f},{11,19,.0003f,-.72f}};
constexpr int modeCount=sizeof(modes)/sizeof(Mode);
constexpr float amplitudeBound() {
    float bound=0;
    for (const auto& mode:modes) bound+=mode.amplitude;
    return bound;
}
constexpr float meniscusHeight=0.0018f, meniscusWidth=0.026f;
constexpr float maxRise=amplitudeBound()+4*meniscusHeight;
static_assert(level+maxRise<top && level-maxRise>bed,"The waves must stay inside the tank");

struct WaveState { Vec3 terms[modeCount]; float meniscusMean; };
WaveState wavesAt(double seconds);
struct Vertex { float y; Vec3 normal; };
// x and z are distances from the inner tank corner. The meniscus has its
// discrete area mean removed so even the triangulated mesh conserves fill volume.
HD inline Vertex sampleWater(const WaveState& waves,float x,float z) {
    float y=level-waves.meniscusMean,sx=0,sz=0;
    for (const auto& term:waves.terms) {
        float sinX=sinf(term.x*x),cosX=cosf(term.x*x);
        float sinZ=sinf(term.y*z),cosZ=cosf(term.y*z);
        y+=term.z*cosX*cosZ;
        sx-=term.z*term.x*sinX*cosZ;
        sz-=term.z*term.y*cosX*sinZ;
    }
    float l=expf(-x/meniscusWidth),r=expf(-(x1-x0-x)/meniscusWidth);
    float b=expf(-z/meniscusWidth),f=expf(-(z1-z0-z)/meniscusWidth);
    y+=meniscusHeight*(l+r+b+f);
    sx+=meniscusHeight/meniscusWidth*(r-l);
    sz+=meniscusHeight/meniscusWidth*(f-b);
    return {y,unit({-sx,1,-sz})};
}
HD inline Vec3 point(int i,float y) { return {x0+(i%stride)*dx,y,z0+(i/stride)*dz}; }
HD inline Triangle face(int id,const Vertex* grid,int material) {
    int cell=id/2, base=(cell/nx)*stride+cell%nx;
    int a=(id&1)?base+1:base,b=base+stride,c=(id&1)?base+stride+1:base+1;
    Vertex flat{level,{0,1,0}};
    Vertex va=grid?grid[a]:flat,vb=grid?grid[b]:flat,vc=grid?grid[c]:flat;
    Vec3 p=point(a,va.y);
    return {p,point(b,vb.y)-p,point(c,vc.y)-p,va.normal,vb.normal,vc.normal,material};
}
}
