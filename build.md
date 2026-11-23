# Developer handoff: CUDA room path tracer

Updated: 2026-11-23.

This document describes the current implementation, how to build and validate it, and where to continue development. See [README.md](README.md) for controls, CLI options, and texture-generation prompts, and [PROFILING.md](PROFILING.md) for measurements and their limitations.

## Current state

The application renders a procedural room with oak flooring, plaster walls, a glass aquarium, animated water, spheres, and a glowing rose with falling petals under a glass cloche. It supports offline BMP/PPM rendering and a Windows interactive viewer.

The latest work is implemented:

- Split the former `main.cu` and included `.inl` files into CPU `.cpp`, CUDA `.cu`, and shared headers. The obsolete source files were removed.
- Added a `renderer_core` static library shared by the application and headless GPU tests.
- Generated oak and plaster image assets and embedded all material textures with mipmaps filtered in linear light.
- Increased the water mesh to 72 x 60 cells and added sixteen wave modes, damping, sustained motion, and a meniscus.
- Added an eight-frame quality burst after camera movement stops, with denoising history invalidated when the view changes.
- Added water, texture-mipmap, and camera-denoising regression tests.

The working directory had no `.git` metadata during this work. There is no associated commit or branch identifier to record.

## Build requirements

- NVIDIA GPU with a compatible driver; CUDA Toolkit 12 or newer.
- CMake 3.23 or newer and a CUDA-compatible C++17 compiler.
- Python 3 with Pillow 9.1 or newer. Python and Pillow are build-time requirements even when tests are disabled because the textures are baked into C++ arrays.
- Windows: Visual Studio 2022, Desktop development with C++, a Windows SDK, and CUDA integration.

The verified environment was Windows, Visual Studio 2022, CUDA 12.9, and an RTX 4090 with compute capability `89`. Linux offline support remains in the code but was not built or tested during this change. There is no CPU rendering fallback.

From the repository root, in PowerShell:

```powershell
python -m pip install "Pillow>=9.1"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_CUDA_ARCHITECTURES=89
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Choose a CUDA architecture appropriate for the target GPU. The project defaults to `75` when no override is supplied. If a build directory already uses a different generator, configure a separate directory instead of changing its generator in place.

The existing presets use a different output directory:

```powershell
cmake --preset vs2022
cmake --build --preset release
ctest --test-dir build/vs2022 -C Release --output-on-failure
.\build\vs2022\Release\path_tracer.exe
```

The non-preset commands produce `build/Release/path_tracer.exe`; preset commands produce `build/vs2022/Release/path_tracer.exe`. Keep the executable and CTest paths consistent.

In the restricted development environment, MSBuild initially failed to read `C:\Users\alex1\AppData\Local\Microsoft SDKs`. Building with permission to access the installed SDK succeeded; this was an environment access issue, not a source compilation error.

## Run and reproduce the preview

```powershell
# Interactive Windows viewer; animation starts automatically.
.\build\Release\path_tracer.exe

# Small deterministic offline check.
.\build\Release\path_tracer.exe --offline --width 480 --height 320 --spp 32 --scene-time 2.5 --output preview.bmp

# Final handoff preview, at a fixed animation pose.
New-Item -ItemType Directory -Force renders | Out-Null
.\build\Release\path_tracer.exe --offline --width 1440 --height 960 --spp 512 --scene-time 2.5 --output renders/textured-room.bmp

# Optional PNG conversion; the renderer itself writes only BMP or PPM.
python -c "from PIL import Image; Image.open('renders/textured-room.bmp').save('renders/textured-room.png')"

# Bounded live run with stage timings.
.\build\Release\path_tracer.exe --profile --frames 90 --width 1280 --height 720
```

Output directories must exist. Use `--scene-time` for reproducible image comparisons, `--no-denoise` for raw samples, and `--no-bvh` for the reference intersection path. Default live rendering is 1280 x 720, 2 samples per frame, and 8 bounces. Offline defaults are 3840 x 2160, 256 samples, and 12 bounces.

## Architecture and ownership

| Area | Main files | Responsibility |
| --- | --- | --- |
| Application | [main.cpp](src/main.cpp), [options.cpp](src/options.cpp), [image_io.cpp](src/image_io.cpp) | Startup, CLI, offline render scheduling, image output |
| Scene construction | [scene_builder.cpp](src/scene_builder.cpp), [rose.cpp](src/rose.cpp), [aquarium_scene.cpp](src/aquarium_scene.cpp) | CPU procedural geometry and materials |
| Acceleration | [bvh_builder.cpp](src/bvh_builder.cpp), [intersection.cuh](src/intersection.cuh) | CPU BVH construction and GPU intersections |
| GPU ownership | [room_scene.h](src/room_scene.h), [scene_upload.cpp](src/scene_upload.cpp), [gpu_resources.h](src/gpu_resources.h) | Scene buffers, textures, instance transforms, resource lifetime |
| Tracing | [render.cu](src/render.cu), [material_sampling.cuh](src/material_sampling.cuh) | GPU path integration, material lookup, adaptive sample counts |
| Water | [water_model.h](src/water_model.h), [water_model.cpp](src/water_model.cpp), [water.cu](src/water.cu), [water_device.cuh](src/water_device.cuh) | Shared surface model, CPU wave parameters, GPU mesh updates and medium transitions |
| Denoising | [denoiser.h](src/denoiser.h), [denoiser.cu](src/denoiser.cu) | Guides, temporal history, spatial filtering |
| Viewer | [live_viewer.cpp](src/live_viewer.cpp), [camera_refinement.h](src/camera_refinement.h) | Windows input, camera state, refinement scheduling |
| Presentation | [display.cu](src/display.cu), [dx_presenter.cpp](src/dx_presenter.cpp), [frame_profiler.h](src/frame_profiler.h) | GPU tone mapping, CUDA/D3D11 transfer, timing; GDI fallback is managed by the viewer |
| Shared contracts | [geometry.h](src/geometry.h), [scene_types.h](src/scene_types.h), [gpu_api.h](src/gpu_api.h) | Math, scene views, and host-callable kernel launch wrappers |

CPU code calls `launchRender`, `launchWaterUpdate`, `launchToneMap`, and the `Denoiser` interface. Keep kernel launch syntax and device implementations in `.cu`/`.cuh` files. The host still uses the CUDA runtime for allocation, transfers, synchronization, and events.

`RoomScene` owns device resources; its `Scene view` contains non-owning device pointers and texture handles. Keep the owner alive while kernels use the view. `DeviceBuffer` provides allocation cleanup. `Denoiser` hides its CUDA implementation behind a private implementation pointer. Existing launches use the default CUDA stream; account for this ordering before introducing multiple streams.

The CPU builds a static room BVH and local BVHs for falling petals. Animation updates petal transforms and world bounds without rebuilding their meshes. Water vertices and triangles update on the GPU; their static BVH bounds include the entire allowed wave excursion. Path, guide, and shadow rays share intersection code.

## Texture pipeline

Build inputs are [rose-petal.png](data/rose-petal.png), [oak-floor.png](data/oak-floor.png), and [lime-plaster.png](data/lime-plaster.png). Oak and plaster were generated with built-in ImageGen; final prompts are preserved in the README. Rebuilding does not call an image-generation service.

[embed_textures.py](tools/embed_textures.py) decodes source sRGB colors, resizes and filters mip levels in linear light, then encodes RGBA bytes into `build/scene_texture_data.cpp`. The base level is 512 x 512. CMake tracks the script and source images as dependencies. Do not hand-edit the generated C++ file.

The GPU sampler performs sRGB decoding. Room materials use world-space projection and repeating textures; petals retain their existing UV coordinates. Set `Material::texture` and `textureScale` when assigning a textured surface. Denoising separates known texture color from lighting before filtering and restores it afterward.

When adding a texture, update `TextureId`/`SceneTextureCount`, the bake inputs and array symbols, GPU upload code, and material assignment together. The current GPU texture resource helper is still named `PetalTexture`, although it also uploads the room textures.

## Water and camera-denoising behavior

The water is an analytic gravity-capillary wave model, not a general fluid solver. Sixteen modes combine large motion with smaller ripples. An envelope models viscous damping and wall losses while retaining weak sustained motion. A 1.8 mm meniscus curves the surface near the glass; its mean offset is removed to preserve fill level within the tested tolerance. Heights and normals come from the same surface function. Changing amplitudes or geometry requires checking `maxRise`, the static BVH envelope, and tank clearances.

On camera movement, the viewer clears accumulation, invalidates temporal history, and rebuilds guides. Movement uses at most two samples per frame. After movement stops, `CameraRefinement` requests at least four samples for eight denoised frames, then returns to `--live-spp`. The sample sequence advances by the actual nominal frame budget so this variable budget does not repeat stable-surface samples. Holding the right mouse button without moving it does not block refinement.

During playback, stable surfaces reuse up to 32 samples; moving or specular regions use current-frame data. Water and petals continue animating during refinement. A scene held with `--scene-time` can progressively accumulate across frames. Disabling denoising also disables the quality burst.

Important denoiser contracts:

- Accumulation and the result returned by `apply()` contain summed radiance, not a normalized mean. Consumers divide by the supplied sample count.
- Call `preparePlayback()` before requesting `historyMask()` or using playback mode, and rebuild guides after playback storage is first allocated.
- Reset history after camera changes, replay, or disabling denoising. Rebuild guides whenever the view or animated geometry changes.
- Adaptive sample allocation must preserve the row budget and use actual per-pixel sample counts when blending history.

## Tests and recorded validation

| CTest name | Coverage |
| --- | --- |
| `adaptive_sampling` | Odd/singleton rows, supported sample budgets, and conservation of paired ray budgets |
| `water_model` | Finite vertices, unit normals, height-gradient agreement, volume tolerance, bounds, repeatability, ongoing motion |
| `camera_denoising` | Bounded refinement, denoise toggle, camera/replay history reset, finite radiance, post-move quality against an independently seeded reference |
| `texture_mipmaps` | Linear-light averaging and preservation of constant colors |
| `acceleration` | 32 byte-identical BVH/linear rendered-image comparisons, animated poses, and held-time determinism |
| `denoising` | Boundary dimensions, deterministic raw output, and at least 20% display RGB MSE reduction against a high-sample reference |

The three GPU rendering tests run serially with 300-second CTest timeouts. Useful focused runs:

```powershell
ctest --test-dir build -C Release -R camera_denoising -V
ctest --test-dir build -C Release -R "water_model|texture_mipmaps" --output-on-failure
```

Recorded during implementation on 2026-11-23:

- Release build succeeded; all six tests passed in about 72 seconds.
- Before visual changes, the CPU/CUDA split produced six byte-identical comparisons against the saved original executable, at three animation times with denoising on and off.
- The final camera-denoising quality check passed separately after it was extended: 30.25% lower mapped RGB error than the final noisy frame. This uses one held 65 x 41 view, a 512-sample independent reference, and the mapping `x / (1 + x)`.
- A hidden 90-frame CUDA/D3D11 run completed at 1280 x 720. After five warmup frames it reported 16.55 ms frame work and 56.54 FPS internal cadence. The initial refinement burst overlaps this window; this is not a visible-window latency measurement.
- The 1440 x 960, 512-sample preview was rendered and visually inspected.

These are prior implementation results, not tests rerun merely to write this handoff.

## Known limitations and useful follow-up work

- Test Linux compilation and offline output; only Windows was validated here.
- Add interactive tests for keyboard/mouse transitions, resize, minimize, and D3D11-to-GDI fallback. The current camera test is headless and the viewer smoke test did not inject camera input.
- The pre-refactor audit found tone-map overflow for extremely large but finite `--exposure` values, such as `1e20`. This was not fixed by the refactor; the relevant code is now in `image_io.cpp` and `display.cu`.
- GPU selection is currently fixed to device 0. There is no CLI GPU selector.
- Low-sample animated glass/water can remain noisy, and center-ray denoising guides can misclassify subpixel silhouettes. Refinement improves quality but does not freeze animation or guarantee a noise-free view.
- The scene remains procedural and specialized around one ceiling area light. The cloche uses thin-glass transmission; aquarium interfaces use explicit media and refraction. Keep these different models in mind when extending materials.
- Offline launches batch eight samples per pixel. Very large resolutions on slower GPUs may require smaller batches to avoid desktop watchdog timeouts.

Local generated artifacts are in ignored directories: `build/` and `renders/`. `build/refactor-baseline/` contains a local pre-refactor source/executable snapshot, and `build/profile-textured-playback.txt` records the smoke-test timings. These may be absent from another checkout and are not build dependencies. Keep source textures, tools, tests, and documentation with the project; regenerate build output and previews as needed.
