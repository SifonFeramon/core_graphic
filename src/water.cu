#include "gpu_api.h"
#include "gpu_resources.h"
namespace tank {
__global__ void sampleWaves(Vertex* grid,WaveState waves) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if (i>=vertices) return;
    grid[i]=sampleWater(waves,(i%stride)*dx,(i/stride)*dz);
}
__global__ void updateFaces(Triangle* triangles,const Vertex* grid,int material) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if (i<faces) triangles[i]=face(i,grid,material);
}

}
void launchWaterUpdate(Triangle* triangles, tank::Vertex* grid, int material, tank::WaveState waves) {
    tank::sampleWaves<<<(tank::vertices+127)/128,128>>>(grid,waves);
    check(cudaGetLastError(),"Update aquarium waves");
    tank::updateFaces<<<(tank::faces+127)/128,128>>>(triangles,grid,material);
    check(cudaGetLastError(),"Update water surface");
}
