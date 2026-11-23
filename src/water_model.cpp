#include "water_model.h"
namespace tank {
WaveState wavesAt(double seconds) {
    WaveState waves;
    for (int i=0;i<modeCount;++i) {
        const auto& m=modes[i];
        float kx=m.x*PI/(x1-x0), kz=m.z*PI/(z1-z0);
        double k=std::hypot(double(kx),double(kz));
        double omega=std::sqrt((9.81*k+0.000072*k*k*k)*std::tanh(k*(level-bed)));
        // Viscous damping plus wall losses; weak sustained excitation keeps the
        // aquarium alive without resetting phase or making large waves perpetual.
        double damping=0.055+2*1.0e-6*k*k;
        double envelope=0.35+0.65*std::exp(-damping*seconds);
        waves.terms[i]={kx,kz,m.amplitude*float(envelope*std::cos(omega*seconds+m.phase))};
    }
    double meanX=0,meanZ=0;
    for (int i=0;i<=nx;++i) {
        double x=i*double(dx);
        meanX+=(i==0 || i==nx?0.5:1)*(std::exp(-x/meniscusWidth)+std::exp(-(x1-x0-x)/meniscusWidth));
    }
    for (int i=0;i<=nz;++i) {
        double z=i*double(dz);
        meanZ+=(i==0 || i==nz?0.5:1)*(std::exp(-z/meniscusWidth)+std::exp(-(z1-z0-z)/meniscusWidth));
    }
    waves.meniscusMean=float(meniscusHeight*(meanX/nx+meanZ/nz));
    return waves;
}

}
