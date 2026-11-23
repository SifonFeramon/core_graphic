#pragma once
#include <cmath>
#include <cstdint>
constexpr float PI = 3.14159265358979323846f;
constexpr float EPS = 0.0002f;
#ifdef __CUDACC__
#define HD __host__ __device__
#else
#define HD
#endif

struct Vec3 {
    float x, y, z;
    HD Vec3(float v = 0) : x(v), y(v), z(v) {}
    HD Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    HD Vec3 operator+(Vec3 b) const { return {x+b.x,y+b.y,z+b.z}; }
    HD Vec3 operator-(Vec3 b) const { return {x-b.x,y-b.y,z-b.z}; }
    HD Vec3 operator-() const { return {-x,-y,-z}; }
    HD Vec3 operator*(float s) const { return {x*s,y*s,z*s}; }
    HD Vec3 operator*(Vec3 b) const { return {x*b.x,y*b.y,z*b.z}; }
    HD Vec3 operator/(float s) const { return *this * (1.0f/s); }
    HD Vec3 operator/(Vec3 b) const { return {x/b.x,y/b.y,z/b.z}; }
    HD Vec3& operator+=(Vec3 b) { *this = *this+b; return *this; }
};
HD inline float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
HD inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
HD inline Vec3 unit(Vec3 a) { return a / sqrtf(dot(a,a)); }
HD inline float largest(Vec3 a) { return fmaxf(a.x,fmaxf(a.y,a.z)); }
struct Ray { Vec3 origin, direction; };
enum MaterialType { DIFFUSE, MIRROR, EMISSIVE, THIN_GLASS, GLASS_OUTER, GLASS_INNER, WATER_SURFACE };
HD inline bool tankInterface(MaterialType type) { return type>=GLASS_OUTER; }
enum TextureId { NoTexture=-1, OakTexture=0, PlasterTexture=1, SceneTextureCount=2 };
struct Material {
    Vec3 color, emission;
    MaterialType type;
    int texture=NoTexture;
    float textureScale=1; // Repetitions per world-space meter.
};
struct Quad { Vec3 origin, u, v, normal; float area; int material; };
struct Sphere { Vec3 center; float radius; int material; };
constexpr int PetalRows=12, PetalColumns=16;
struct Triangle {
    Vec3 origin, edgeU, edgeV;
    Vec3 normalA, normalB, normalC;
    int material;
    int petalFace=-1; // Triangle in the petal's regular UV grid, shared by all poses.
};
