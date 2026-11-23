#pragma once
#include "geometry.h"
#include <optional>
#include <string>
#include <vector>
struct Options {
    int width=0,height=0,spp=256,bounces=0;
    uint32_t seed=42; float exposure=1.0f;
    std::string output="room.bmp";
    bool offline=false;
    bool denoise=true;
    bool profile=false;
    bool useBvh=true;
    std::optional<float> sceneTime;
    int liveSpp=2;
    int frames=0; // Optional bounded run for checking the live viewer.
};
Options parse(int argc,char** argv);
void save(const Options&,const std::vector<Vec3>&);
class RoomScene;
int runLiveViewer(RoomScene&,const Options&);
