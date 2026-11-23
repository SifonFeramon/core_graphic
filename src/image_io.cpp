#include "app.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>
unsigned char encode(float value,float exposure) {
    float x=fmaxf(0,value*exposure);
    // ACES fitted tone curve, followed by the standard sRGB transfer function.
    x=std::clamp((x*(2.51f*x+0.03f))/(x*(2.43f*x+0.59f)+0.14f),0.0f,1.0f);
    x=x<=0.0031308f?12.92f*x:1.055f*std::pow(x,1/2.4f)-0.055f;
    return static_cast<unsigned char>(std::clamp(x*255+0.5f,0.0f,255.0f));
}
void save(const Options& o,const std::vector<Vec3>& pixels) {
    std::ofstream out(o.output,std::ios::binary);
    if (!out) throw std::runtime_error("Cannot create "+o.output);
    bool bmp=o.output.substr(o.output.size()-4)==".bmp";
    uint32_t stride=(uint32_t(o.width)*3+3)&~3u;
    if (bmp) {
        auto u16=[&](uint16_t v) { out.put(char(v&255));out.put(char(v>>8)); };
        auto u32=[&](uint32_t v) { for (int i=0;i<4;++i) out.put(char((v>>(8*i))&255)); };
        out.write("BM",2);u32(54+stride*o.height);u16(0);u16(0);u32(54);
        u32(40);u32(o.width);u32(o.height);u16(1);u16(24);u32(0);u32(stride*o.height);
        u32(2835);u32(2835);u32(0);u32(0);
    } else out<<"P6\n"<<o.width<<' '<<o.height<<"\n255\n";
    for (int row=0;row<o.height;++row) {
        int y=bmp?o.height-1-row:row;
        for (int x=0;x<o.width;++x) {
            Vec3 c=pixels[size_t(y)*o.width+x]/float(o.spp);
            unsigned char rgb[]={encode(c.x,o.exposure),encode(c.y,o.exposure),encode(c.z,o.exposure)};
            if (bmp) { out.put(char(rgb[2]));out.put(char(rgb[1]));out.put(char(rgb[0])); }
            else out.write(reinterpret_cast<char*>(rgb),3);
        }
        if (bmp) for (uint32_t p=o.width*3;p<stride;++p) out.put(0);
    }
    out.close();
    if (!out) throw std::runtime_error("Failed writing "+o.output);
}
