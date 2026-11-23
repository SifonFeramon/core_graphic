#pragma once
#include "scene_types.h"
#include "bounds.h"
#include <algorithm>
#include <limits>
#include <vector>
struct BvhBuilder {
    struct Bounds {
        Vec3 low{std::numeric_limits<float>::max()},high{-std::numeric_limits<float>::max()};
        void include(const Bounds& b) { low=minimum(low,b.low);high=maximum(high,b.high); }
        float area() const { Vec3 d=high-low;return 2*(d.x*d.y+d.x*d.z+d.y*d.z); }
    };
    std::vector<BvhNode> nodes;
    std::vector<int> primitives;
    std::vector<Bounds> bounds;

    static Bounds triangleBounds(const Triangle& triangle) {
        Vec3 a=triangle.origin,b=a+triangle.edgeU,c=a+triangle.edgeV;
        return {minimum(a,minimum(b,c))-Vec3(EPS),maximum(a,maximum(b,c))+Vec3(EPS)};
    }

    BvhBuilder(const std::vector<Quad>& quads,const std::vector<Sphere>& spheres,
               const std::vector<Triangle>& triangles,int waterMaterial=-1);
    int build(int first,int count);
};
