#pragma once
enum PlaybackClass : uint8_t { CurrentFrame, StableSurface, FineDetail };

// Pair opposite halves of each row. Each extra detail ray is paid for by its stable partner.
HD inline int playbackSampleCount(const uint8_t* mask,int index,int x,int width,int nominal) {
    if (!mask || nominal<2) return nominal;
    int halfWidth=width/2;
    if (x>=2*halfWidth) return nominal;
    int partner=x<halfWidth?index+halfWidth:index-halfWidth;
    uint8_t here=mask[index],there=mask[partner];
    if (here==StableSurface && there==FineDetail) return nominal-nominal/2;
    if (here==FineDetail && there==StableSurface) return nominal+nominal/2;
    return nominal;
}
