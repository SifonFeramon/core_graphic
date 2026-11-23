#pragma once
#include "geometry.h"
#include "water_model.h"
#include <cuda_runtime.h>
struct BvhNode {
    Vec3 low, high;
    int first, count, escape; // Preorder layout: escape skips the entire subtree.
};
struct MeshPose {
    Vec3 origin, along, up, across;

    MeshPose() = default;
    MeshPose(Vec3 anchor, float yaw, float pitch, float roll) : origin(anchor) {
        along={std::cos(yaw)*std::cos(pitch),std::sin(pitch),std::sin(yaw)*std::cos(pitch)};
        Vec3 side{-std::sin(yaw),0,std::cos(yaw)};
        Vec3 top=cross(side,along);
        up=top*std::cos(roll)+side*std::sin(roll);
        across=side*std::cos(roll)-top*std::sin(roll);
    }

    HD Vec3 rotate(Vec3 v) const { return along*v.x+up*v.y+across*v.z; }
    HD Vec3 localPoint(Vec3 point) const {
        Vec3 delta=point-origin;
        return localDirection(delta);
    }
    HD Vec3 localDirection(Vec3 v) const {
        return {dot(v,along),dot(v,up),dot(v,across)};
    }
    Triangle transform(const Triangle& triangle) const {
        return {origin+rotate(triangle.origin),rotate(triangle.edgeU),rotate(triangle.edgeV),
                rotate(triangle.normalA),rotate(triangle.normalB),rotate(triangle.normalC),triangle.material,triangle.petalFace};
    }
};

struct PetalInstance {
    MeshPose pose;
    Vec3 low, high;
    const Triangle* triangles;
    const BvhNode* nodes;
    const int* order;
    int nodeCount, triangleCount;
};
struct Glow { Vec3 center, emission; float radius; };
struct Scene {
    const Quad* quads;
    const Sphere* spheres;
    const Triangle* triangles;
    const Material* materials;
    int quadCount, sphereCount, triangleCount, lightIndex;
    const BvhNode* nodes;
    const int* primitives;
    int nodeCount;
    bool useBvh;
    Glow glow;
    Vec3 motionLow, motionHigh;
    const PetalInstance* petals;
    int petalCount;
    Vec3 petalLow,petalHigh;
    cudaTextureObject_t petalTexture{};
    float petalTexelsPerMeter{};
    const tank::Vertex* waterGrid{};
    bool petalsPlaying{};
    cudaTextureObject_t textures[SceneTextureCount]{};
};
struct Hit {
    float t; Vec3 position, normal, geometricNormal; int material, quad;
    float2 petalUv{-1,0};
};

struct Camera { Vec3 position, forward, right, up; float tanHalfFov, aspect; };
constexpr float RoomCameraPitch=-0.15f;

Camera makeCamera(Vec3 position,float yaw,float pitch,float aspect);
