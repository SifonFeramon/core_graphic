#include "scene_builder.h"
// Edit this function to construct a different scene. Units are approximately meters; +Y is up.
SceneBuilder makeRoom(RoseAnimation& rose) {
    SceneBuilder b;
    int plaster=b.diffuse({0.76f,0.75f,0.70f});
    int floor=b.diffuse({0.92f,0.92f,0.92f});
    int red=b.diffuse({0.62f,0.12f,0.09f});
    int teal=b.diffuse({0.10f,0.42f,0.40f});
    int trim=b.diffuse({0.88f,0.86f,0.78f});
    int gold=b.diffuse({0.82f,0.52f,0.12f});
    b.materials[floor].texture=OakTexture;
    b.materials[floor].textureScale=0.50f;
    for (int id : {plaster,red,teal,trim}) {
        b.materials[id].texture=PlasterTexture;
        b.materials[id].textureScale=1.25f;
    }
    int lamp=int(b.materials.size()); b.materials.push_back({{}, {13.0f,11.5f,9.0f},EMISSIVE});
    b.quad({-3,0,-4},{6,0,0},{0,0,7},floor);
    b.quad({-3,3.4f,-4},{0,0,7},{6,0,0},plaster);
    b.quad({-3,0,-4},{0,0,7},{0,3.4f,0},red);
    b.quad({3,0,-4},{0,3.4f,0},{0,0,7},teal);
    b.quad({-3,0,3},{6,0,0},{0,3.4f,0},plaster);
    // Back wall assembled around two real openings (no glass): x [-2.35,-.65] and [.65,2.35].
    b.quad({-3,0,-4},{6,0,0},{0,1.3f,0},plaster);
    b.quad({-3,2.65f,-4},{6,0,0},{0,0.75f,0},plaster);
    for (auto span : {Vec3(-3,-2.35f,0),Vec3(-0.65f,0.65f,0),Vec3(2.35f,3,0)})
        b.quad({span.x,1.3f,-4},{span.y-span.x,0,0},{0,1.35f,0},plaster);
    for (float x : {-2.35f,0.65f}) {
        b.box({x-0.065f,1.24f,-4.08f},{x+1.765f,1.32f,-3.82f},trim); // sill
        b.box({x-0.065f,2.65f,-4.06f},{x+1.765f,2.72f,-3.93f},trim);
        for (float side : {x-0.065f,x+1.7f})
            b.box({side,1.3f,-4.06f},{side+0.065f,2.65f,-3.93f},trim);
        b.box({x+0.825f,1.3f,-4.04f},{x+0.875f,2.65f,-3.94f},trim);
    }
    b.box({-0.95f,3.30f,-1.95f},{0.95f,3.38f,-0.55f},trim);
    b.lightIndex=int(b.quads.size());
    b.quad({-0.85f,3.285f,-1.85f},{1.7f,0,0},{0,0,1.2f},lamp); // cross(u,v) points down
    b.box({-1.95f,0,-2.35f},{-0.55f,0.65f,-0.95f},plaster);
    addAquarium(b);
    b.spheres.push_back({{1.15f,0.72f,-1.65f},0.72f,gold});
    b.spheres.push_back({{1.60f,0.34f,0.4f},0.34f,teal});
    addRose(b,rose,{0,0,-0.32f});
    return b;
}

Camera makeCamera(Vec3 position, float yaw, float pitch, float aspect) {
    Vec3 forward{std::sin(yaw)*std::cos(pitch),std::sin(pitch),-std::cos(yaw)*std::cos(pitch)};
    Vec3 right=unit(cross(forward,Vec3(0,1,0)));
    return {position,forward,right,cross(right,forward),std::tan(62.0f*PI/360),aspect};
}
