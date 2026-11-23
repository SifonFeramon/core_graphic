#include "mesh_surface.h"
void addAquarium(SceneBuilder& room) {
    using namespace tank;
    int outer=int(room.materials.size());
    room.materials.push_back({{1,1,1},{},GLASS_OUTER});
    int inner=int(room.materials.size());
    room.materials.push_back({{1,1,1},{},GLASS_INNER});
    room.waterMaterial=int(room.materials.size());
    room.materials.push_back({{1,1,1},{},WATER_SURFACE});
    int mat=room.diffuse({.032f,.042f,.045f});
    room.box({left,.6501f,back},{right,bottom-2*EPS,front},mat);

    // One manifold glass shell: outer and inner walls, bottom, and the open rim.
    auto walls=[&](float l,float r,float b,float f,float y,int material) {
        Vec3 across{r-l,0,0},along{0,0,f-b},up{0,top-y,0};
        room.quad({l,y,b},across,up,material);
        room.quad({l,y,f},across,up,material);
        room.quad({l,y,b},along,up,material);
        room.quad({r,y,b},along,up,material);
        room.quad({l,y,b},across,along,material);
    };
    walls(left,right,back,front,bottom,outer);
    walls(x0,x1,z0,z1,bottom+pane,inner);
    room.quad({left,top,back},{right-left,0,0},{0,0,pane},outer);
    room.quad({left,top,z1},{right-left,0,0},{0,0,pane},outer);
    room.quad({left,top,z0},{pane,0,0},{0,0,z1-z0},outer);
    room.quad({x1,top,z0},{pane,0,0},{0,0,z1-z0},outer);

    int sand=room.diffuse({.60f,.48f,.33f});
    room.box({x0+2*EPS,bottom+pane+2*EPS,z0+2*EPS},{x1-2*EPS,bed,z1-2*EPS},sand);
    int stones[]={room.diffuse({.17f,.20f,.19f}),room.diffuse({.37f,.31f,.24f}),
                  room.diffuse({.42f,.43f,.38f}),room.diffuse({.24f,.21f,.18f})};
    // Partly buried, flattened river gravel gives the refraction visible landmarks.
    uint32_t seed=8271;
    auto random=[&]() { seed=1664525u*seed+1013904223u; return float(seed>>8)/16777216.0f; };
    for (int i=0;i<24;++i) {
        float x=x0+.10f+random()*(x1-x0-.20f);
        float z=z0+.09f+random()*(z1-z0-.18f);
        float radius=.018f+.051f*random(),wide=.63f+.28f*random(),angle=PI*random();
        appendSurface(room.triangles,6,10,stones[i%4],[=](float t,float s) {
            float h=cosf(PI*t),r=radius*sinf(PI*t);
            float a=PI*s, px=r*cosf(a)*(1+.09f*sinf(3*a)),pz=wide*r*sinf(a);
            return Vec3{x+cosf(angle)*px-sinf(angle)*pz,bed+.44f*radius*(h+.15f),
                        z+sinf(angle)*px+cosf(angle)*pz};
        });
    }
    room.firstWaterFace=int(room.triangles.size());
    for (int i=0;i<faces;++i) room.triangles.push_back(face(i,nullptr,room.waterMaterial));
}
