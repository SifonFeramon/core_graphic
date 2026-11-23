#pragma once
#include "scene_builder.h"
template<class Surface>
void appendSurface(std::vector<Triangle>& triangles, int rows, int columns,
                   int material, const Surface& position, bool petal=false) {
    struct Vertex { Vec3 point, normal; };
    int stride=columns+1;
    std::vector<Vertex> vertices((rows+1)*stride);
    for (int row=0;row<=rows;++row) {
        float t=float(row)/rows;
        // Sample normals just inside the surface at pointed tips.
        float sample=std::clamp(t,0.002f,0.998f);
        for (int column=0;column<=columns;++column) {
            float side=2.0f*column/columns-1;
            Vec3 along=position(sample+0.001f,side)-position(sample-0.001f,side);
            Vec3 across=position(sample,side+0.001f)-position(sample,side-0.001f);
            vertices[row*stride+column]={position(t,side),unit(cross(along,across))};
        }
    }
    auto append=[&](const Vertex& a, const Vertex& b, const Vertex& c, int face) {
        Vec3 u=b.point-a.point,v=c.point-a.point,n=cross(u,v);
        if (dot(n,n)>1e-16f) triangles.push_back({a.point,u,v,a.normal,b.normal,c.normal,material,petal?face:-1});
    };
    for (int row=0;row<rows;++row) for (int column=0;column<columns;++column) {
        int a=row*stride+column;
        int face=2*(row*columns+column);
        append(vertices[a],vertices[a+stride],vertices[a+1],face);
        append(vertices[a+stride],vertices[a+stride+1],vertices[a+1],face+1);
    }
}
