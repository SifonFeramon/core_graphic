#include "mesh_surface.h"
#include <iterator>
#include <utility>
std::vector<Triangle> makeLeaf(float length, float halfWidth, int material) {
    std::vector<Triangle> mesh;
    appendSurface(mesh,12,4,material,[=](float t, float side) {
        float envelope=t*(1-t)*4;
        float teeth=0.94f+0.06f*std::cos(12*PI*t);
        float width=halfWidth*envelope*(1.2f-0.42f*t)*teeth;
        float fold=envelope*halfWidth*(0.44f-0.31f*std::fabs(side)-0.15f*side*side);
        return Vec3{length*t,fold-0.12f*length*t*t,width*side};
    });
    return mesh;
}

Vec3 roseStem(Vec3 base, float t) {
    return base+Vec3(-0.17f*std::sin(PI*t)+0.035f*t*t,1.38f*t,0.04f*std::sin(1.3f*PI*t));
}

void addRose(SceneBuilder& room, RoseAnimation& animation, Vec3 base) {
    int wood=room.diffuse({0.32f,0.17f,0.081f});
    room.materials[wood].color={0.65f,0.48f,0.34f};
    room.materials[wood].texture=OakTexture;
    room.materials[wood].textureScale=1.5f;
    int stalk=room.diffuse({0.066f,0.21f,0.107f});
    int foliage=room.diffuse({0.073f,0.30f,0.135f});
    int petalMaterials[]={room.diffuse({0.94f,0.90f,1.00f},{0.24f,0.010f,0.086f}),
                          room.diffuse({1.00f,0.98f,1.00f},{0.35f,0.016f,0.125f}),
                          room.diffuse({0.78f,0.72f,0.91f},{0.16f,0.007f,0.063f})};
    int glass=int(room.materials.size());
    room.materials.push_back({{0.56f,0.67f,0.79f},{},THIN_GLASS});
    constexpr float deck=0.17f;
    const Vec3 planting=base+Vec3(0,deck,0);
    // Include room for subpixel rays at the edges of the falling petals.
    room.motionLow=base+Vec3(-0.77f,0.12f,-0.77f);
    room.motionHigh=base+Vec3(0.77f,2.13f,0.77f);

    const Vec3 profile[]={
        {0,0.001f,0},{0.773f,0.001f,0},{0.801f,0.022f,0},{0.801f,0.054f,0},
        {0.772f,0.072f,0},{0.772f,0.112f,0},{0.790f,0.132f,0},{0.784f,0.151f,0},
        {0.747f,deck,0},{0,deck,0}
    };
    constexpr int spans=int(sizeof(profile)/sizeof(profile[0]))-1;
    appendSurface(room.triangles,spans,40,wood,[&](float t, float side) {
        float index=t*spans;
        int segment=std::min(int(index),spans-1);
        Vec3 p=profile[segment]+(profile[segment+1]-profile[segment])*(index-segment);
        float a=PI*side;
        return base+Vec3(p.x*std::cos(a),p.y,p.x*std::sin(a));
    });
    appendSurface(room.triangles,28,48,glass,[&](float t, float side) {
        float cap=PI*std::max(0.0f,t-0.5f);
        float radius=0.715f*std::max(0.0f,std::cos(cap));
        float height=1.33f*std::min(1.0f,2*t)+0.77f*std::sin(cap);
        float angle=PI*side;
        return planting+Vec3(radius*std::cos(angle),height,radius*std::sin(angle));
    });
    room.spheres.push_back({planting+Vec3(0,2.122f,0),0.044f,wood});
    appendSurface(room.triangles,18,8,stalk,[&](float t, float side) {
        float a=PI*side,radius=0.020f-0.007f*t;
        return roseStem(planting,t)+Vec3(radius*std::cos(a),0,radius*std::sin(a));
    });

    Vec3 blossom=roseStem(planting,1);
    Vec3 up=unit(Vec3(0.27f,0.86f,0.43f));
    Vec3 x=unit(cross(up,Vec3(0,0,1))),z=cross(x,up);
    room.glow={blossom+up*0.20f,{0.29f,0.012f,0.145f},0.44f};
    struct Drop { int index; Vec3 landing; float yaw, release, duration; };
    const Drop drops[]={
        {2,{-0.38f,0,0.24f},2.20f,0.85f,2.02f},
        {0,{0.36f,0,0.24f},0.78f,1.79f,2.15f},
        {3,{-0.19f,0,0.00f},4.48f,2.72f,2.12f},
        {1,{0.00f,0,0.43f},1.52f,3.53f,2.26f}
    };
    // Leave a small scatter of static leaves on the stem. The bloom loses outer petals.
    auto attach=[&](Vec3 point,float yaw,float pitch,float length,float width,int material) {
        MeshPose pose(point,yaw,pitch,0);
        for (const auto& triangle : makeLeaf(length,width,material))
            room.triangles.push_back(pose.transform(triangle));
    };
    for (int i=0;i<5;++i)
        attach(blossom,0.2f+2*PI*i/5,0.49f,0.135f,0.025f,stalk);
    attach(roseStem(planting,0.57f),0.56f,-0.55f,0.37f,0.086f,foliage);
    attach(roseStem(planting,0.68f),2.78f,-0.28f,0.24f,0.069f,foliage);
    attach(roseStem(planting,0.40f),0.96f,-0.69f,0.28f,0.077f,stalk);

    struct Layer { int count; float angle, radius, height, width, curl, lip; };
    const Layer layers[]={
        {7,0.36f,0.321f,0.334f,0.80f,0.15f,0.110f},
        {6,0.82f,0.277f,0.386f,0.93f,0.25f,0.080f},
        {5,0.32f,0.213f,0.436f,1.01f,0.38f,0.052f},
        {4,0.77f,0.145f,0.438f,1.25f,0.47f,0.035f},
        {3,0.20f,0.083f,0.433f,1.52f,0.62f,0.025f}
    };
    for (const Layer& layer : layers) for (int i=0;i<layer.count;++i) {
        float angle=layer.angle+2*PI*i/layer.count;
        float reach=layer.radius*(1+0.10f*std::sin(2.63f*i+layer.angle));
        float height=layer.height*(1+0.09f*std::cos(3.08f*i+layer.angle));
        auto shape=[&](float t,float side) {
            float s2=side*side,s4=s2*s2;
            // Shorten the shoulders and roll the free edge outward into a rounded lip.
            t*=1-0.15f*s4;
            float t2=t*t,t4=t2*t2,rim=t4*t2;
            float spread=layer.width*(0.075f+0.925f*std::sin(PI*t*0.5f));
            float a=angle+spread*side+layer.curl*t;
            float r=0.014f+reach*(std::sin(1.71f*t)*(1-0.21f*s2*t)+0.082f*rim)+0.010f*side*t4;
            float h=height*(1.43f*t-0.43f*t2)-layer.lip*rim*(0.62f+0.25f*s2)
                    +0.014f*t4*std::sin(4.1f*side+angle);
            return blossom+x*(r*std::cos(a))+up*h+z*(r*std::sin(a));
        };
        int material=petalMaterials[(i+int(&layer-layers))%3];
        const Drop* drop=&layer==layers?std::find_if(std::begin(drops),std::end(drops),
                                                [=](const Drop& candidate) { return candidate.index==i; })
                                       :std::end(drops);
        if (drop==std::end(drops)) {
            appendSurface(room.triangles,PetalRows,PetalColumns,material,shape,true);
            continue;
        }
        Vec3 attachment=shape(0.62f,0);
        MeshPose attached(attachment,angle,0,0);
        FallingPetal petal{{},attachment,planting+drop->landing,angle,drop->yaw,drop->release,drop->duration};
        appendSurface(petal.mesh,PetalRows,PetalColumns,material,[&](float t,float side) {
            return attached.localPoint(shape(t,side));
        },true);
        animation.duration=std::max(animation.duration,drop->release+drop->duration);
        animation.petals.push_back(std::move(petal));
    }

    int mote=room.diffuse({0.39f,0.022f,0.14f},{0.70f,0.085f,0.30f});
    room.spheres.push_back({planting+Vec3(0.37f,1.25f,0.18f),0.010f,mote});
    room.spheres.push_back({planting+Vec3(0.29f,0.96f,0.35f),0.008f,mote});
    room.spheres.push_back({planting+Vec3(-0.28f,1.08f,0.31f),0.009f,mote});
    room.spheres.push_back({planting+Vec3(0.16f,0.56f,0.38f),0.011f,mote});

    animation.floor=planting.y;
}
