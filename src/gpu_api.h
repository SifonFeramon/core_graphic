#pragma once
#include "scene_types.h"
void launchRender(Scene scene,Camera camera,Vec3* accumulation,int width,int height,
                  int firstSample,int sampleCount,int bounces,uint32_t seed,
                  const uint8_t* historyMask=nullptr,uint32_t sampleOffset=0);
void launchToneMap(const Vec3* accumulation,uint32_t* display,int count,int samples,float exposure);
void launchWaterUpdate(Triangle* triangles,tank::Vertex* grid,int material,tank::WaveState waves);
