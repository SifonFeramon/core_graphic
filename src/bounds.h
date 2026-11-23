#pragma once
#include "geometry.h"
HD inline float component(Vec3 v,int axis) { return axis==0?v.x:axis==1?v.y:v.z; }
HD inline Vec3 minimum(Vec3 a,Vec3 b) { return {fminf(a.x,b.x),fminf(a.y,b.y),fminf(a.z,b.z)}; }
HD inline Vec3 maximum(Vec3 a,Vec3 b) { return {fmaxf(a.x,b.x),fmaxf(a.y,b.y),fmaxf(a.z,b.z)}; }
