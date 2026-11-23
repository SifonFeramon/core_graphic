# CUDA room path tracer

A standalone C++17 / CUDA renderer. The scene is an enclosed room with two open windows, a warm ceiling light, textured oak flooring and plaster walls, a glass aquarium on a cube, two diffuse spheres, and a glowing pink rose under a glass cloche at the center. The open windows admit light from a blue sky environment.

The GPU traces jittered camera rays with diffuse scattering, mirror reflection, thin glass, multiple indirect bounces, Russian roulette, shadow rays, and multiple importance sampling for the ceiling light. A GPU denoiser runs before ACES fitted tone mapping and sRGB encoding. The rose is modeled from the still in `data/rose.jfif`, and the renderer generates all scene geometry at startup. The generated [petal color texture](data/rose-petal.png) follows each petal from base to rim, including during its fall. The executable embeds a 512 × 512 copy with filtered mip levels for distant views.

<details>
<summary>Petal texture generation prompt</summary>

Built-in ImageGen prompt for `data/rose-petal.png`

> Photorealistic rose petal base-color texture for a 3D material. Fill the entire square, edge to edge, with the flattened surface of one deep raspberry-pink rose petal. Its narrow attachment is at bottom center, fine irregular veins fan gently upward and outward, and the broad upper rim is a slightly lighter rosy pink. Bring out subtle velvety grain and natural pigment variation. An evenly lit, cross-polarized botanical macro scan. Only petal tissue in the frame, with no background or silhouette, cast shadows, shine, glow, water droplets, or text.

</details>

## Source layout

See [build.md](build.md) for the developer handoff, build commands, implementation contracts, validation results, and known limitations.

CPU host code is compiled as ordinary C++ in `.cpp` files. CUDA translation units contain GPU kernels and their launch wrappers. The `renderer_core` library is shared by the application and headless rendering tests.

| Files | Responsibility |
| --- | --- |
| `main.cpp`, `options.cpp`, `image_io.cpp` | Entry point, CLI parsing, BMP/PPM output |
| `scene_builder.cpp`, `rose.cpp`, `aquarium_scene.cpp` | CPU scene and mesh generation |
| `bvh_builder.cpp`, `scene_upload.cpp`, `water_model.cpp` | CPU BVH construction, GPU resource ownership, wave parameters |
| `live_viewer.cpp`, `dx_presenter.cpp` | Windows input, camera refinement, D3D11/GDI presentation |
| `render.cu`, `intersection.cuh`, `material_sampling.cuh` | GPU ray tracing, visibility, textured materials |
| `denoiser.cu`, `water.cu`, `display.cu` | GPU denoising, animated water mesh, tone mapping |
| `gpu_api.h`, `denoiser.h` | Host-callable interface; no kernel launch syntax in CPU files |

## Requirements

- An NVIDIA CUDA-capable GPU and driver compatible with your CUDA toolkit.
- CUDA Toolkit 12 or newer, CMake 3.23 or newer, and a CUDA-compatible C++ compiler.
- Building from source requires Python 3 and Pillow 9.1 or newer to bake the material textures. Install Pillow with `python -m pip install "Pillow>=9.1"`.
- On Windows: Visual Studio 2022 with Desktop development with C++ and CUDA's Visual Studio integration. Run commands from a Developer PowerShell if using Ninja or `nvcc` directly.

## Build

```powershell
cmake -S . -B build -DCMAKE_CUDA_ARCHITECTURES=89
cmake --build build --config Release --parallel
```

The example targets an RTX 4090 (`89`). Replace it with your GPU's compute capability, such as `86` for an RTX 3080. CMake 3.24 or newer also accepts `native` to detect the local GPU. Without an override the project targets `75` (Turing and later through embedded PTX).

## Render

```powershell
# Windows, Visual Studio generator
.\build\Release\path_tracer.exe

# Save a denoised image instead of opening the live viewer
.\build\Release\path_tracer.exe --offline --output room.bmp

# Explicit 4K UHD render (also the offline default)
.\build\Release\path_tracer.exe --offline --width 3840 --height 2160 --output room-4k.bmp

# Quick preview
.\build\Release\path_tracer.exe --offline --width 480 --height 320 --spp 32 --output preview.bmp

# Cleaner render (more samples take longer)
.\build\Release\path_tracer.exe --offline --width 1440 --height 960 --spp 1024 --bounces 16 --output room.bmp
```

On Windows, the live viewer renders at 1280 × 720 by default. CUDA writes the frame to a Direct3D 11 texture on the same GPU. The viewer uses GDI as a fallback when interoperation is unavailable. Use `--width` and `--height` to set the internal resolution. The image fits between the controls while preserving its aspect ratio. Offline renders default to 3840 × 2160.

Move with WASD, Q/E for down/up, arrow keys or the right mouse button to look, Shift to move faster, and R to reset the camera. Press N to toggle denoising without resetting accumulated samples. The status bar shows the actual render resolution and whether denoising is enabled. Moving the camera rebuilds the guides and discards old-view history. After movement stops, eight denoised frames use at least four samples per pixel to refine the view; the status bar shows **Refining** during this burst. Holding the right mouse button still does not prevent refinement. Water and petals keep moving. Resizing the window preserves accumulated samples.

The aquarium waves and rose animation start when the viewer opens. Four petals detach in turn, flutter inside the cloche and settle on the round base. The water keeps moving after they land. Press P or click **Replay P** to restart both. Use `--scene-time 2.5` to freeze the water and petals at the same instant. Offline renders use time zero by default.

For Linux or a single-configuration generator, the executable is `./build/path_tracer`. Linux always renders to a file. BMP is directly viewable in normal image viewers. `--output room.ppm` writes binary P6 PPM instead. Output directories must already exist.

| Option | Default | Meaning |
| --- | --- | --- |
| `--width` / `--height` | 1280 / 720 live, 3840 / 2160 offline | Internal render and saved image resolution (up to 16384 per dimension) |
| `--spp` | 256 | Samples per pixel |
| `--live-spp` | 2 | Normal samples per live frame (1–8); capped at 2 during camera movement, at least 4 for eight denoised frames after movement stops |
| `--bounces` | 8 live, 12 offline | Maximum path depth (up to 128) |
| `--exposure` | 1.0 | Linear exposure multiplier before tone mapping |
| `--seed` | 42 | Positive integer random seed |
| `--output` | room.bmp | Output filename ending in `.bmp` or `.ppm` |
| `--offline` | off | Save an image instead of opening the Windows live viewer |
| `--no-denoise` | off | Disable denoising for raw render output or the initial live view |
| `--no-bvh` | off | Use the reference linear intersection loop for validation or benchmarking |
| `--frames` | unlimited | Close the Windows live viewer after this many rendered frames |
| `--scene-time` | live playback / offline 0 | Hold the waves and petals at the given time in seconds |
| `--profile` | off | Report live CUDA stage times and CPU frame latency on exit |
| `--help` | | Show usage |

## Live performance and profiling

The viewer budgets two samples per pixel per frame by default. During denoised playback with a stationary camera, a stable pixel paired with a glass, water, or luminous petal pixel gives it one sample. The pair receives one and three samples, preserving the total ray budget. At one sample per pixel, every pixel receives one ray. The denoiser combines up to 32 samples across frames on stable surfaces and uses the actual sample counts when blending. Moving petals, water and reflections use current-frame data. Moving the camera or replaying the animation clears the history. Holding the scene with `--scene-time` enables progressive accumulation. `--spp` controls saved images.

Profile a stationary scene for 90 frames:

```powershell
.\build\Release\path_tracer.exe --profile --frames 90 --scene-time 0
.\build\Release\path_tracer.exe --profile --frames 90 --scene-time 0 --live-spp 1
.\build\Release\path_tracer.exe --profile --frames 90 --scene-time 0 --no-denoise
```

Profiling uses CUDA events and CPU wall timers. It excludes the first five frames and reports tracing, guides, denoising, tone mapping, display transfer, and presentation. Frame work includes waiting for the CUDA frame to finish. Viewer frame cadence also includes the 16 ms limiter. The startup log identifies the display backend. See [benchmark results](PROFILING.md).

## Denoising

Denoising is enabled by default for both saved images and the live viewer. The dependency-free CUDA implementation uses three edge-avoiding à-trous passes with steps of 1, 2, and 4 pixels. Surface material, normal, tangent-plane distance, and HDR luminance guide the filter. Center rays follow reflections and transmission to locate the visible surface. When a ray through the cloche sees only sky, the glass surface guides the filter. Filtering strength decreases with sample count. [Dammertz et al., HPG 2010](https://jo.dreggn.org/home/2010_atrous.pdf)

The filter uses separate GPU buffers and preserves the path tracing accumulation. It filters lighting separately from texture color to retain wood grain, plaster detail, and petal veins. Spatial filtering uses about 68 bytes of GPU storage per pixel, with an extra 17 bytes for live playback history. That history reuses samples at the same pixel for a stationary camera. Very low sample counts can still produce blotches in motion-sensitive areas, and center-ray guides can leave artifacts along subpixel silhouettes.

Compare identical samples with denoising disabled:

```powershell
.\build\Release\path_tracer.exe --offline --spp 32 --output denoised.bmp
.\build\Release\path_tracer.exe --offline --spp 32 --no-denoise --output raw.bmp
```

Run the render quality regression check with Python 3 (standard library only):

```powershell
python tests/verify_denoising.py build/Release/path_tracer.exe
python tests/verify_acceleration.py build/Release/path_tracer.exe
# Run all six checks, including water, texture mips, and camera history resets
ctest --test-dir build -C Release --output-on-failure
```

## Change the scene

### Generated material textures

The built-in ImageGen tool generated `data/oak-floor.png` and `data/lime-plaster.png` for this project. Oak is used on the floor and wooden cloche base; plaster is used on the room surfaces. `tools/embed_textures.py` bakes these and the petal texture into the executable at 512 × 512 with mipmaps filtered in linear light. CUDA uses world-space coordinates and distance-filtered sampling. The PNG files are build inputs; no external image files are needed at runtime.

<details>
<summary>Final oak texture prompt (built-in ImageGen)</summary>

> Use case: photorealistic-natural. Asset type: seamless square base-color texture for a physically based 3D renderer. Generate an orthographic cross-polarized material scan of warm natural oak floor boards, filling the entire image edge to edge. Several long parallel boards running vertically, staggered end joints, fine believable wood grain and occasional small knots, restrained honey brown color and modest natural variation. Tileable at all four edges. Flat diffuse illumination with absolutely no baked shadows, specular highlights, perspective, border, objects, text or watermark. The image is only the wood surface, not a room.

</details>

<details>
<summary>Final plaster texture prompt (built-in ImageGen)</summary>

> Use case: photorealistic-natural. Asset type: seamless square base-color material texture for an interior path tracer. Orthographic cross-polarized scan of fine pale warm-white lime plaster, filling the frame edge to edge. Realistic very fine granular mineral texture, subtle irregular trowel marks and delicate mottling. Low contrast, neutral cream-white overall. Tileable on all four edges. Uniform diffuse illumination, no baked directional shadows, no specular highlights, cracks, seams, perspective, borders, objects, text or watermark. Only flat plaster material, not a wall in a room.

</details>

Edit `makeRoom()` in `src/scene_builder.cpp`. Coordinates use meters approximately, with +Y up. `SceneBuilder::quad(origin, u, v, material)` creates a rectangle with **orthogonal** edge vectors; `box(min, max, material)` creates six rectangles. Add spheres through `builder.spheres`. Diffuse materials use RGB reflectance values between 0 and 1. The ceiling light is the single emissive quad marked by `lightIndex`; its edge-vector cross product must point into the room. The initial camera is set in `main.cpp` and `live_viewer.cpp`; `makeCamera()` in `scene_builder.cpp` sets the field of view. Set `Material::texture` and `textureScale` to apply world-space material textures.

The rose, cloche, and petal motion are defined in `src/rose.cpp`. The room has a static BVH and each falling petal has its own local-space BVH. Animation updates the four instance transforms and their bounds. Path, shadow, and denoising guide rays share the same intersection code. The acceleration test compares BVH and linear rendering, including during the petal drop.

The open aquarium is defined in `src/aquarium_scene.cpp`. Its 12 mm glass walls enclose air above the water and water below it. Rays can cross all three media, bend at each interface, reflect internally and lose color according to distance traveled. Submerged surfaces also sample paths from the ceiling light through the moving water surface. [Dielectric reflection and transmission](https://www.pbr-book.org/4ed/Reflection_Models/Specular_Reflection_and_Transmission)

The analytic wave model in `src/water_model.cpp` and `src/water_model.h` combines sixteen gravity-capillary modes on a 72 × 60 grid. Smaller ripples, viscous damping, and wall losses soften the initial motion; gentle sustained excitation keeps the surface moving. A 1.8 mm meniscus curves the water near the glass, with its mean height removed to preserve the fill volume. CUDA updates the mesh and analytic normals in `src/water.cu`; static BVH bounds cover the complete wave excursion. The water test checks volume, bounds, normals, and repeatability. [MIT, surface gravity waves](https://ocw.mit.edu/courses/12-802-wave-motion-in-the-ocean-and-the-atmosphere-spring-2008/fca533a5edb5f5d3e975ec4c2e63019f_MIT12_802S08_lec03.pdf)

The cloche uses a thin glass model with angle-dependent reflections and straight transmission. A finite bounce limit truncates indirect illumination. Offline kernel launches batch eight samples per pixel, so very large resolutions or slower GPUs may need a smaller batch to avoid desktop watchdog timeouts.
