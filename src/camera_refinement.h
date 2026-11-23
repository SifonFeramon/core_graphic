#pragma once
#include <algorithm>

// A short quality burst after an actual camera change. Holding a mouse button
// still is not movement; animation continues throughout refinement.
class CameraRefinement {
    int remaining=0;
public:
    static constexpr int RefinementFrames=8;
    int sampleBudget(bool cameraChanged,bool denoise,int nominal) {
        if (cameraChanged) {
            remaining=RefinementFrames;
            return std::min(2,nominal);
        }
        if (!denoise) { remaining=0;return nominal; }
        if (remaining>0) { --remaining;return std::max(4,nominal); }
        return nominal;
    }
    bool active() const { return remaining>0; }
};
