#include "room_scene.h"
#include "gpu_api.h"
RoomScene::RoomScene(bool useBvh)
        : host(makeRoom(rose)), acceleration(host.quads,host.spheres,host.triangles,host.waterMaterial),
          quads(host.quads.size()), spheres(host.spheres.size()), triangles(host.triangles.size()),
          materials(host.materials.size()), nodes(acceleration.nodes.size()), primitives(acceleration.primitives.size()),
          deviceInstances(std::max(size_t(1),rose.petals.size())),
          view{quads.data,spheres.data,triangles.data,materials.data,int(host.quads.size()),int(host.spheres.size()),
               int(host.triangles.size()),host.lightIndex,nodes.data,primitives.data,int(acceleration.nodes.size()),
               useBvh,host.glow,host.motionLow,host.motionHigh,deviceInstances.data,int(rose.petals.size()),{}, {}} {
        petalTexture.upload();
        view.petalTexture=petalTexture.handle;
        sceneTextures[OakTexture].upload(oakPixels,true);
        sceneTextures[PlasterTexture].upload(plasterPixels,true);
        for (int i=0;i<SceneTextureCount;++i) view.textures[i]=sceneTextures[i].handle;
        view.waterGrid=waterGrid.data;
        // Approximate UV density of the 0.35 m petals for distance filtering.
        view.petalTexelsPerMeter=petalTextureSize/0.35f;
        quads.upload(host.quads);
        spheres.upload(host.spheres);
        triangles.upload(host.triangles);
        materials.upload(host.materials);
        nodes.upload(acceleration.nodes);
        primitives.upload(acceleration.primitives);
        for (const auto& petal : rose.petals) {
            meshes.push_back(std::make_unique<PetalMesh>(petal));
            const auto& mesh=*meshes.back();
            instances.push_back({{}, {}, {},mesh.triangles.data,mesh.nodes.data,mesh.order.data,
                                 int(mesh.tree.nodes.size()),int(petal.mesh.size())});
        }
        setTime(0);
    }
void RoomScene::setTime(float seconds) {
        if (time==seconds) return;
        view.petalsPlaying=seconds<rose.duration;
        launchWaterUpdate(triangles.data+host.firstWaterFace,waterGrid.data,host.waterMaterial,tank::wavesAt(seconds));
        bool changed=false;
        BvhBuilder::Bounds group;
        for (size_t i=0;i<instances.size();++i) {
            auto& instance=instances[i];
            const auto& petal=rose.petals[i];
            float progress=petal.progress(seconds);
            if (time<0 || progress!=petal.progress(time)) {
                instance.pose=petal.poseAt(progress,rose.floor);
                const auto& root=meshes[i]->tree.nodes.front();
                BvhBuilder::Bounds world;
                for (int corner=0;corner<8;++corner) {
                    Vec3 p{corner&1?root.high.x:root.low.x,corner&2?root.high.y:root.low.y,
                           corner&4?root.high.z:root.low.z};
                    Vec3 point=instance.pose.origin+instance.pose.rotate(p);
                    world.include({point,point});
                }
                instance.low=world.low-Vec3(EPS);
                instance.high=world.high+Vec3(EPS);
                changed=true;
            }
            group.include({instance.low,instance.high});
        }
        time=seconds;
        if (changed) {
            view.petalLow=group.low;
            view.petalHigh=group.high;
            deviceInstances.upload(instances);
        }
    }
