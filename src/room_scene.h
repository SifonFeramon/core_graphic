#pragma once
#include "scene_builder.h"
#include "bvh_builder.h"
#include "gpu_resources.h"
#include <memory>
struct PetalMesh {
    BvhBuilder tree;
    DeviceBuffer<Triangle> triangles;
    DeviceBuffer<BvhNode> nodes;
    DeviceBuffer<int> order;
    explicit PetalMesh(const FallingPetal& petal) : tree({}, {},petal.mesh),
        triangles(petal.mesh.size()), nodes(tree.nodes.size()), order(tree.primitives.size()) {
        triangles.upload(petal.mesh);
        nodes.upload(tree.nodes);
        order.upload(tree.primitives);
    }
};

class RoomScene {
    RoseAnimation rose;
    SceneBuilder host;
    BvhBuilder acceleration;
    DeviceBuffer<Quad> quads;
    DeviceBuffer<Sphere> spheres;
    DeviceBuffer<Triangle> triangles;
    DeviceBuffer<Material> materials;
    DeviceBuffer<BvhNode> nodes;
    DeviceBuffer<int> primitives;
    std::vector<std::unique_ptr<PetalMesh>> meshes;
    std::vector<PetalInstance> instances;
    DeviceBuffer<PetalInstance> deviceInstances;
    DeviceBuffer<tank::Vertex> waterGrid{tank::vertices};
    PetalTexture petalTexture;
    PetalTexture sceneTextures[SceneTextureCount];
    float time=-1;

public:
    Scene view;

    explicit RoomScene(bool useBvh);
    void setTime(float seconds);
};
