#pragma once
#include "scene_types.h"
#include "bounds.h"
#include "water_device.cuh"
namespace tank {
struct Interface {
    Medium next;
    float eta, reflectance;
    Vec3 normal,transmitted;
};
__device__ inline Interface interfaceAt(const Scene& scene,MaterialType type,Medium from,const Hit& h,Vec3 incident) {
    Interface s{};
    s.next=otherMedium(type,from,scene.waterGrid,h.position);
    s.eta=ior(from)/ior(s.next);
    s.normal=h.normal;
    s.reflectance=refract(incident,s.normal,s.eta,s.transmitted);
    if (s.reflectance<1 && dot(s.transmitted,h.geometricNormal)>=0) {
        s.normal=h.geometricNormal;
        s.reflectance=refract(incident,s.normal,s.eta,s.transmitted);
    }
    return s;
}
__device__ inline Ray scatterRay(const Hit& h,Vec3 incident,const Interface& s,bool reflect) {
    if (!reflect) return {h.position-h.geometricNormal*(2*EPS),s.transmitted};
    Vec3 reflected=incident-s.normal*(2*dot(incident,s.normal));
    if (dot(reflected,h.geometricNormal)<=0)
        reflected=incident-h.geometricNormal*(2*dot(incident,h.geometricNormal));
    return {h.position+h.geometricNormal*(2*EPS),unit(reflected)};
}
}

__device__ inline bool triangleCoordinates(const Triangle& triangle, Ray ray, float& t, float& u, float& v) {
    Vec3 p=cross(ray.direction,triangle.edgeV);
    float determinant=dot(triangle.edgeU,p);
    if (fabsf(determinant)<1e-9f) return false;
    float inverse=1.0f/determinant;
    Vec3 offset=ray.origin-triangle.origin;
    u=dot(offset,p)*inverse;
    if (u<0 || u>1) return false;
    Vec3 q=cross(offset,triangle.edgeU);
    v=dot(ray.direction,q)*inverse;
    if (v<0 || u+v>1) return false;
    t=dot(triangle.edgeV,q)*inverse;
    return t>EPS;
}

__device__ inline Hit triangleHit(const Triangle& triangle, Ray ray, float t, float u, float v) {
    Vec3 geometric=unit(cross(triangle.edgeU,triangle.edgeV));
    Vec3 smooth=unit(triangle.normalA*(1-u-v)+triangle.normalB*u+triangle.normalC*v);
    if (dot(geometric,ray.direction)>0) {
        geometric=-geometric;
        smooth=-smooth;
    }
    if (dot(smooth,ray.direction)>=0 || dot(smooth,geometric)<=0) smooth=geometric;
    Hit hit{t,ray.origin+ray.direction*t,smooth,geometric,triangle.material,-1};
    if (triangle.petalFace>=0) {
        int cell=triangle.petalFace/2;
        bool second=(triangle.petalFace&1)!=0;
        // The two triangles share a diagonal. Image row zero is the broad free rim.
        float column=float(cell%PetalColumns)+(second?u+v:v);
        float row=float(cell/PetalColumns)+(second?1-v:u);
        hit.petalUv={column/PetalColumns,1-row/PetalRows};
    }
    return hit;
}

__device__ inline float glassReflectance(Vec3 normal, Vec3 direction) {
    float grazing=1-fminf(1,fabsf(dot(normal,direction)));
    float square=grazing*grazing;
    float surface=0.04f+0.96f*square*square*grazing;
    // Combined reflection of the two interfaces of a thin sheet of glass.
    return 2*surface/(1+surface);
}

__device__ inline bool blocksShadow(const Scene& scene, const Triangle& triangle, Ray ray,
                             float t, float u, float v, float& transmission) {
    if (scene.materials[triangle.material].type!=THIN_GLASS) return true;
    Hit glass=triangleHit(triangle,ray,t,u,v);
    transmission*=1-glassReflectance(glass.normal,ray.direction);
    return false;
}

__device__ inline bool intersectLinear(const Scene& scene, Ray ray, Hit& hit, float maxT,
                                float* transmission) {
    hit.t = maxT; bool found = false;
    for (int i=0;i<scene.quadCount;++i) {
        const Quad& q = scene.quads[i];
        float denominator = dot(q.normal,ray.direction);
        if (fabsf(denominator)<1e-7f) continue;
        float t = dot(q.origin-ray.origin,q.normal)/denominator;
        if (t <= EPS || t >= hit.t) continue;
        Vec3 d = ray.origin+ray.direction*t-q.origin;
        // Scene quads have orthogonal edge vectors.
        float u=dot(d,q.u)/dot(q.u,q.u), v=dot(d,q.v)/dot(q.v,q.v);
        if (u<0 || u>1 || v<0 || v>1) continue;
        if (transmission) return true;
        Vec3 normal=denominator<0?q.normal:-q.normal;
        hit = {t,ray.origin+ray.direction*t,normal,normal,q.material,i};
        found = true;
    }
    for (int i=0;i<scene.sphereCount;++i) {
        const Sphere& s = scene.spheres[i];
        Vec3 oc=ray.origin-s.center;
        float b=dot(oc,ray.direction), c=dot(oc,oc)-s.radius*s.radius;
        float discriminant=b*b-c;
        if (discriminant<0) continue;
        float root=sqrtf(discriminant), t=-b-root;
        if (t<=EPS) t=-b+root;
        if (t<=EPS || t>=hit.t) continue;
        if (transmission) return true;
        Vec3 p=ray.origin+ray.direction*t, n=(p-s.center)/s.radius;
        if (dot(n,ray.direction)>=0) n=-n;
        hit={t,p,n,n,s.material,-1}; found=true;
    }
    for (int i=0;i<scene.triangleCount;++i) {
        const Triangle& triangle=scene.triangles[i];
        float t,u,v;
        if (!triangleCoordinates(triangle,ray,t,u,v) || t>=hit.t) continue;
        if (transmission) {
            if (blocksShadow(scene,triangle,ray,t,u,v,*transmission)) return true;
            continue;
        }
        hit=triangleHit(triangle,ray,t,u,v);
        found=true;
    }
    return found;
}

__device__ inline bool intersectsBounds(Vec3 lower,Vec3 upper,Ray ray,Vec3 inverse,float maxT) {
    float nearT=EPS,farT=maxT;
    for (int axis=0;axis<3;++axis) {
        float origin=component(ray.origin,axis),direction=component(ray.direction,axis);
        float low=component(lower,axis),high=component(upper,axis);
        // Explicit parallel handling avoids 0*infinity when the origin is on a slab.
        if (direction==0) { if (origin<low || origin>high) return false;continue; }
        float a=(low-origin)*component(inverse,axis),b=(high-origin)*component(inverse,axis);
        nearT=fmaxf(nearT,fminf(a,b));farT=fminf(farT,fmaxf(a,b));
        if (nearT>farT) return false;
    }
    return true;
}

template<bool ShadowRay>
__device__ inline bool intersectBvh(const Scene& scene,Ray ray,Hit& hit,float maxT,float* transmission) {
    hit.t=maxT;
    int closest=-1;
    Vec3 inverse{1.0f/ray.direction.x,1.0f/ray.direction.y,1.0f/ray.direction.z};
    for (int nodeIndex=0;nodeIndex<scene.nodeCount;) {
        const BvhNode& node=scene.nodes[nodeIndex];
        if (!intersectsBounds(node.low,node.high,ray,inverse,hit.t)) { nodeIndex=node.escape;continue; }
        ++nodeIndex;
        for (int p=0;p<node.count;++p) {
            int primitive=scene.primitives[node.first+p];
            if (primitive<scene.quadCount) {
                const Quad& q=scene.quads[primitive];
                float denominator=dot(q.normal,ray.direction);
                if (fabsf(denominator)<1e-7f) continue;
                float t=dot(q.origin-ray.origin,q.normal)/denominator;
                if (t<=EPS || t>hit.t || (t==hit.t && (closest<0 || primitive>=closest))) continue;
                Vec3 d=ray.origin+ray.direction*t-q.origin;
                float u=dot(d,q.u)/dot(q.u,q.u),v=dot(d,q.v)/dot(q.v,q.v);
                if (u<0 || u>1 || v<0 || v>1) continue;
                if (ShadowRay) return true;
                Vec3 normal=denominator<0?q.normal:-q.normal;
                hit={t,ray.origin+ray.direction*t,normal,normal,q.material,primitive};
            } else if (primitive<scene.quadCount+scene.sphereCount) {
                const Sphere& s=scene.spheres[primitive-scene.quadCount];
                Vec3 oc=ray.origin-s.center;
                float b=dot(oc,ray.direction),c=dot(oc,oc)-s.radius*s.radius;
                float discriminant=b*b-c;
                if (discriminant<0) continue;
                float root=sqrtf(discriminant),t=-b-root;
                if (t<=EPS) t=-b+root;
                if (t<=EPS || t>hit.t || (t==hit.t && (closest<0 || primitive>=closest))) continue;
                if (ShadowRay) return true;
                Vec3 position=ray.origin+ray.direction*t,normal=(position-s.center)/s.radius;
                if (dot(normal,ray.direction)>=0) normal=-normal;
                hit={t,position,normal,normal,s.material,-1};
            } else {
                const Triangle& triangle=scene.triangles[primitive-scene.quadCount-scene.sphereCount];
                float t,u,v;
                if (!triangleCoordinates(triangle,ray,t,u,v)) continue;
                if (t>hit.t || (t==hit.t && (closest<0 || primitive>=closest))) continue;
                if (ShadowRay) {
                    if (blocksShadow(scene,triangle,ray,t,u,v,*transmission)) return true;
                    continue;
                }
                hit=triangleHit(triangle,ray,t,u,v);
            }
            closest=primitive;
        }
    }
    return closest>=0;
}

// Petals are rigid, opaque meshes. Only their poses and top-level bounds change.
__device__ inline bool intersectPetal(const PetalInstance& petal,Ray worldRay,Hit& hit,bool shadow,bool useBvh) {
    Ray ray{petal.pose.localPoint(worldRay.origin),petal.pose.localDirection(worldRay.direction)};
    Vec3 inverse{1/ray.direction.x,1/ray.direction.y,1/ray.direction.z};
    int closest=-1;
    float bestU=0,bestV=0;
    for (int nodeIndex=0;nodeIndex<(useBvh?petal.nodeCount:1);) {
        int first=0,count=petal.triangleCount;
        if (useBvh) {
            const BvhNode& node=petal.nodes[nodeIndex];
            if (!intersectsBounds(node.low,node.high,ray,inverse,hit.t)) {nodeIndex=node.escape;continue;}
            first=node.first;count=node.count;
        }
        ++nodeIndex;
        for (int i=first;i<first+count;++i) {
            int id=useBvh?petal.order[i]:i;
            float t,u,v;
            if (!triangleCoordinates(petal.triangles[id],ray,t,u,v)) continue;
            if (t>hit.t || (t==hit.t && (closest<0 || id>=closest))) continue;
            if (shadow) return true;
            closest=id;hit.t=t;bestU=u;bestV=v;
        }
    }
    if (closest<0) return false;
    hit=triangleHit(petal.triangles[closest],ray,hit.t,bestU,bestV);
    hit.position=worldRay.origin+worldRay.direction*hit.t;
    hit.normal=petal.pose.rotate(hit.normal);
    hit.geometricNormal=petal.pose.rotate(hit.geometricNormal);
    return true;
}

__device__ inline bool intersect(const Scene& scene,Ray ray,Hit& hit,float maxT=1e30f,float* transmission=nullptr) {
    bool found;
    if (!scene.useBvh) found=intersectLinear(scene,ray,hit,maxT,transmission);
    else if (transmission) found=intersectBvh<true>(scene,ray,hit,maxT,transmission);
    else found=intersectBvh<false>(scene,ray,hit,maxT,nullptr);
    if ((transmission && found) || !scene.petalCount) return found;
    Vec3 inverse{1/ray.direction.x,1/ray.direction.y,1/ray.direction.z};
    if (scene.useBvh && !intersectsBounds(scene.petalLow,scene.petalHigh,ray,inverse,hit.t)) return found;
    for (int i=0;i<scene.petalCount;++i) {
        const auto& petal=scene.petals[i];
        if (scene.useBvh && !intersectsBounds(petal.low,petal.high,ray,inverse,hit.t)) continue;
        if (intersectPetal(petal,ray,hit,transmission!=nullptr,scene.useBvh)) {
            if (transmission) return true;
            found=true;
        }
    }
    return found;
}
