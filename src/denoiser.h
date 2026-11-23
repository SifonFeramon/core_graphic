#pragma once
#include "scene_types.h"
#include <memory>
enum class DenoiseMode { Spatial, Playback, Settling };
class Denoiser {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    Denoiser(int width,int height);
    ~Denoiser();
    bool preparePlayback();
    void resetHistory();
    const uint8_t* historyMask() const;
    void updateGuides(Scene scene,Camera camera);
    const Vec3* apply(const Vec3* accumulation,int samples,DenoiseMode mode=DenoiseMode::Spatial);
};
