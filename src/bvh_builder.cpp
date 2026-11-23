#include "bvh_builder.h"
BvhBuilder::BvhBuilder(const std::vector<Quad>& quads,const std::vector<Sphere>& spheres,
               const std::vector<Triangle>& triangles,int waterMaterial) {
        bounds.reserve(quads.size()+spheres.size()+triangles.size());
        for (const auto& q : quads) {
            Vec3 low=minimum(minimum(q.origin,q.origin+q.u),minimum(q.origin+q.v,q.origin+q.u+q.v));
            Vec3 high=maximum(maximum(q.origin,q.origin+q.u),maximum(q.origin+q.v,q.origin+q.u+q.v));
            // Pad flat rectangles and absorb roundoff at primitive boundaries.
            bounds.push_back({low-Vec3(EPS),high+Vec3(EPS)});
        }
        for (const auto& s : spheres)
            bounds.push_back({s.center-Vec3(s.radius+EPS),s.center+Vec3(s.radius+EPS)});
        for (const auto& triangle : triangles) {
            Bounds box=triangleBounds(triangle);
            if (triangle.material==waterMaterial) {
                box.low.y=tank::level-tank::maxRise-EPS;
                box.high.y=tank::level+tank::maxRise+EPS;
            }
            bounds.push_back(box);
        }
        for (int i=0;i<int(bounds.size());++i) primitives.push_back(i);
        nodes.reserve(bounds.size()*2);
        if (!primitives.empty()) build(0,int(primitives.size()));
        // Keep GPU buffers valid even for an empty primitive list.
        if (nodes.empty()) { nodes.push_back({{}, {},0,0,1});primitives.push_back(0); }
    }
int BvhBuilder::build(int first,int count) {
        Bounds parent;
        for (int i=first;i<first+count;++i) parent.include(bounds[primitives[i]]);
        int node=int(nodes.size());
        nodes.push_back({parent.low,parent.high,first,count,0});
        float bestCost=parent.area()*count;
        int bestAxis=-1,bestSplit=0;
        std::vector<int> ordered(count),bestOrder;
        std::vector<float> leftArea(count);
        if (count>2) for (int axis=0;axis<3;++axis) {
            std::copy(primitives.begin()+first,primitives.begin()+first+count,ordered.begin());
            std::sort(ordered.begin(),ordered.end(),[&](int a,int b) {
                float ca=component(bounds[a].low+bounds[a].high,axis);
                float cb=component(bounds[b].low+bounds[b].high,axis);
                return ca==cb?a<b:ca<cb;
            });
            Bounds left,right;
            for (int i=0;i<count;++i) { left.include(bounds[ordered[i]]);leftArea[i]=left.area(); }
            for (int split=count-1;split>0;--split) {
                right.include(bounds[ordered[split]]);
                float cost=parent.area()+leftArea[split-1]*split+right.area()*(count-split);
                if (cost<bestCost) {
                    bestCost=cost;bestAxis=axis;bestSplit=split;bestOrder=ordered;
                }
            }
        }
        if (bestAxis>=0) {
            std::copy(bestOrder.begin(),bestOrder.end(),primitives.begin()+first);
            nodes[node].count=0;
            build(first,bestSplit);
            build(first+bestSplit,count-bestSplit);
        }
        nodes[node].escape=int(nodes.size());
        return node;
    }
