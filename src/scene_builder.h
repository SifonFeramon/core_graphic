#pragma once
#include "scene_types.h"
#include <algorithm>
#include <vector>
struct SceneBuilder {
    std::vector<Quad> quads;
    std::vector<Sphere> spheres;
    std::vector<Triangle> triangles;
    std::vector<Material> materials;
    Glow glow{};
    Vec3 motionLow, motionHigh;
    int lightIndex=-1;
    int firstWaterFace=-1,waterMaterial=-1;
    int diffuse(Vec3 color, Vec3 emission={}) {
        materials.push_back({color,emission,DIFFUSE});
        return int(materials.size())-1;
    }
    void quad(Vec3 origin, Vec3 u, Vec3 v, int material) {
        Vec3 n=cross(u,v); float area=std::sqrt(dot(n,n));
        quads.push_back({origin,u,v,n/area,area,material});
    }
    void box(Vec3 low, Vec3 high, int material) {
        float x=high.x-low.x,y=high.y-low.y,z=high.z-low.z;
        quad(low,{x,0,0},{0,y,0},material);
        quad({low.x,low.y,high.z},{0,y,0},{x,0,0},material);
        quad(low,{0,0,z},{x,0,0},material);
        quad({low.x,high.y,low.z},{x,0,0},{0,0,z},material);
        quad(low,{0,y,0},{0,0,z},material);
        quad({high.x,low.y,low.z},{0,0,z},{0,y,0},material);
    }
};

struct FallingPetal {
    std::vector<Triangle> mesh;
    Vec3 attachment, landing;
    float yaw, landingYaw, release, duration;

    float progress(float time) const { return std::clamp((time-release)/duration,0.0f,1.0f); }

    MeshPose poseAt(float fraction, float floor) const {
        float eased=fraction*fraction*(3-2*fraction);
        float flutter=4*fraction*(1-fraction);
        float sway=flutter*std::sin(4*PI*fraction+yaw);
        Vec3 outward{std::cos(yaw),0,std::sin(yaw)};
        Vec3 sideways{-outward.z,0,outward.x};
        Vec3 anchor=attachment+(landing-attachment)*eased
                    +outward*(0.11f*flutter)+sideways*(0.095f*sway);
        MeshPose pose(anchor,yaw+(landingYaw-yaw)*eased+0.60f*sway,
                      -0.79f*eased+0.70f*flutter*std::sin(4*PI*fraction),
                      0.86f*flutter*std::sin(6*PI*fraction+yaw));
        // Only the support height is needed to settle this rigid mesh on the base.
        float lowest=anchor.y;
        for (const Triangle& triangle : mesh) {
            float y=anchor.y+pose.rotate(triangle.origin).y;
            lowest=std::min({lowest,y,y+pose.rotate(triangle.edgeU).y,y+pose.rotate(triangle.edgeV).y});
        }
        pose.origin.y+=std::max(0.0f,floor+2*EPS-lowest);
        return pose;
    }
};

struct RoseAnimation {
    std::vector<FallingPetal> petals;
    float floor=0;
    float duration=0;

};

SceneBuilder makeRoom(RoseAnimation& rose);
void addRose(SceneBuilder&,RoseAnimation&,Vec3);
void addAquarium(SceneBuilder&);
