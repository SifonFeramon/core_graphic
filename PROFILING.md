# Live renderer performance

## Textured materials and host/CUDA split (2026-11-23)

The renderer now builds a shared `renderer_core` library with CPU `.cpp` sources and separate tracing, denoising, water, and display `.cu` files. Before changing materials or water, six 192 × 128 comparisons at 0, 2.5, and 8 seconds, with denoising enabled and disabled, were byte-identical to the saved original executable.

The updated scene adds oak/plaster textures, linear-light mip generation, a 72 × 60 water mesh with sixteen damped modes and a meniscus, and eight refinement frames after camera motion stops. All six CTest checks passed, including 32 BVH/linear comparisons. The camera test verifies that reset history matches a fresh denoiser and reports 30.25% lower mapped RGB error after refinement than the final noisy frame, against an independently seeded 512-sample reference. This is one held pose at 65 × 41, with radiance mapped as `x / (1 + x)`; it is not a general image-quality guarantee.

A hidden Windows live-viewer smoke test used an RTX 4090, CUDA 12.9, Release, 1280 × 720, default animated playback, 2 normal samples per frame, 8 bounces, and denoising. CUDA/D3D11 presentation initialized and all 90 frames completed. After the profiler's five-frame warmup, it reported 16.55 ms average frame work, 13.91 ms tracing, 1.68 ms guides, 0.62 ms denoising, and 56.54 FPS internal frame cadence. The initial refinement burst overlaps the measurement window. This single hidden-window run validates the rendering path; it is not a visible-window latency or monitor-refresh measurement.

The earlier measurements below describe previous versions of the scene and are retained as historical results.

## Animated rose release

Measured on an RTX 4090 with CUDA 12.9, the Windows x64 Release build, the default camera, denoising enabled, two samples per pixel per frame, and eight bounces. Both executables used the same command-line settings. Each figure comes from one run per executable after five warmup frames.

| Workload | Previous frame work | Release frame work | Release viewer FPS |
| --- | ---: | ---: | ---: |
| 1280 × 720, 400 frames starting with playback | 25.66 ms | 13.86 ms | 58.7 |
| 1920 × 1080, 80 frames held at 2.5 seconds | 43.85 ms | 25.90 ms | 38.6 |

The viewer FPS includes the 16 ms limiter. The previous profiler recorded only frame work. Playback uses wall-clock time, so the two 720p runs sampled different proportions of moving and settled frames. The 1080p run held the geometry fixed. At 1080p, tracing went from 30.77 to 24.07 ms per frame and CPU time spent presenting went from 11.13 to 0.65 ms.

The release uses a static BVH for the room and a local BVH for each falling petal. Windows presentation copies the CUDA output into a Direct3D 11 texture on the same GPU. During playback the sampler transfers rays from stable surfaces to paired fine-detail pixels while preserving the frame budget.

In a separate quality comparison, both samplers rendered at 641 × 361 and two samples per pixel with empty history. Against an independently seeded 256-sample reference, adaptive sampling reduced mean squared display RGB error in pixels classified as fine detail by 8.0%, 7.8%, and 7.6% at 1.25, 2.50, and 3.75 seconds respectively. These measurements cover individual poses and selected pixels.

```powershell
.\build\Release\path_tracer.exe --profile --frames 400 --width 1280 --height 720 --live-spp 2 --bounces 8
.\build\Release\path_tracer.exe --profile --frames 80 --width 1920 --height 1080 --live-spp 2 --bounces 8 --scene-time 2.5
```

## BVH acceleration

The renderer now builds a surface-area heuristic bounding volume hierarchy (BVH) once on the CPU. Preorder nodes carry subtree escape indices, so CUDA traversal needs no per-thread stack. The BVH covers both quads and spheres and accelerates path, shadow, and denoising guide intersections. Exact equal-distance hits retain the original primitive ordering. `--no-bvh` selects the independent reference linear loop.

Measured on 2026-11-18 with the RTX 4090, CUDA 12.9, Release build, default room camera, and 12 bounces. Resolution, sample counts, random streams, denoising, and presentation settings were held constant. The baseline executable was saved before refactoring.

| Workload | Original linear renderer | BVH renderer | Speedup |
| --- | ---: | ---: | ---: |
| Offline 1920 x 1080, 64 spp, render time (median of 3) | 0.32 s | 0.14 s | 2.29x |
| Offline 3840 x 2160, 128 spp, render time (median of 3) | 2.55 s | 1.13 s | 2.26x |
| Offline 3840 x 2160, 128 spp, process wall time (median of 3) | 3.143 s | 1.743 s | 1.80x |
| Live 4K, 1 spp/frame, tracing GPU | 21.980 ms | 9.709 ms | 2.26x |
| Live 4K, 1 spp/frame, CPU frame work | 32.829 ms | 20.914 ms | 1.57x |

Offline benchmarks disabled denoising in both executables. Render time uses the application's existing CUDA-event interval, including batch synchronization and gaps between launches; process wall time also includes startup, download, tone mapping, and file writing. The 4K offline runs alternated baseline and BVH, three runs of each. Both resolutions produced byte-identical images against the original executable.

Each live executable ran once for 90 stationary frames with denoising enabled; statistics exclude five warmup frames. Reciprocal CPU frame work improved from 30.460 to 47.815 work FPS; this excludes the limiter and does not measure monitor refresh or input latency. Denoising and presentation remain a larger fraction of the accelerated frame, so the whole-frame gain is smaller than the tracing gain. Live measurements are single-run observations and can vary with desktop activity.

Validation passed: Release build, 28 byte-identical BVH/linear image comparisons, and the existing denoising quality regression (67.2% display RGB MSE reduction). Tests are also registered with CTest when Python is available at configuration time:

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\path_tracer.exe --profile --frames 90
.\build\Release\path_tracer.exe --profile --frames 90 --no-bvh
```

The sections below record the earlier live batching optimization, before the BVH was introduced.

Measured on 2026-11-18 using an RTX 4090, CUDA 12.9, the Release build, the default room camera, 12 bounces, and 3840 × 2160 internal resolution. Each run used 90 live frames, excluding the first five frames from reported statistics. Runs were sequential, without concurrent benchmark render processes.

## Findings

Switching the default image from 960 × 640 to 3840 × 2160 increased pixel count by 13.5×. The old stationary live loop also traced four samples per pixel before presenting: 33.2 million camera paths per frame, plus indirect and shadow rays. The scene uses brute-force intersection loops; this is the main compute bottleneck. The denoiser is a relatively small part of total frame time at four samples per frame.

| Stage, average ms/frame | Before, 4 spp/frame | After, 4 spp/frame | New default, 1 spp/frame |
| --- | ---: | ---: | ---: |
| Path tracing, GPU | 79.616 | 79.877 | 21.084 |
| Denoising, GPU | 5.105 | 4.934 | 4.909 |
| Tone mapping, GPU | 0.148 | 0.149 | 0.148 |
| Download timeline | 1.827 | 1.807 | 1.833 |
| GDI presentation, CPU | 2.587 | 2.686 | 2.624 |
| Total CPU frame work | 89.343 | 89.614 | 30.655 |
| Frame work p95 | 91.253 | 91.077 | 31.625 |
| Reciprocal average work time, FPS | 11.193 | 11.159 | 32.621 |

Disabling denoising at four samples per frame produced 84.234 ms/frame (11.872 work FPS). An original four-sample 1920 × 1080 run measured 24.303 ms/frame (41.148 work FPS). Denoiser removal alone does not solve the 4K tracing cost.

## Changes

- Added opt-in `--profile` event and CPU timing instrumentation. There are no profiling CUDA events when the flag is absent.
- Changed the default stationary live batch to one sample and exposed `--live-spp 1` through `--live-spp 8`. Moving frames already used one sample. Native 4K rendering, 12 bounces, and the denoiser are preserved.
- Shadow rays now terminate at the first occluder instead of finding the nearest one. This is valid for binary visibility and produced byte-identical output in the fixed-seed offline comparison. It gave no measurable speedup at the benchmark camera; the displayed FPS improvement comes from smaller frame batches.

The new default presents partial results about 2.9× more often in this workload. It does not make each path cheaper: using frame work times, accumulation throughput is roughly 33 spp/second versus 45 spp/second at four samples per frame, because denoising and presentation occur more often. Use `--live-spp 4` when stationary convergence matters more than refresh frequency. Saved image rendering is unaffected by live batch settings.

## Reproduce

```powershell
cmake --build build --config Release --parallel
.\build\Release\path_tracer.exe --profile --frames 90
.\build\Release\path_tracer.exe --profile --frames 90 --live-spp 4
.\build\Release\path_tracer.exe --profile --frames 90 --live-spp 4 --no-denoise
python tests/verify_denoising.py build/Release/path_tracer.exe
```

The CUDA event stages share the application's default stream. Download timeline includes host staging; the blocking `cudaMemcpy` CPU measurement includes waiting for earlier GPU kernels and must not be counted again as additional GPU work. The report excludes the frame limiter, startup, and initial guide generation. These are stationary-camera timings, not a moving-camera or end-to-end input latency measurement. Window compositing, GPU clock state, and other GPU clients can affect results.

Nsight Systems is installed, but its capture command returned `Access is denied` in this environment, so the measurements use application CUDA events. These measurements identify expensive application stages; they do not establish per-instruction utilization or occupancy. A BVH or another spatial acceleration structure is the next substantial renderer optimization to investigate, since intersection work still dominates.

## Validation

The Release build passed. The fixed-seed 192 × 128, 64 spp raw render was byte-identical before and after the shadow-ray change. The denoising regression passed, including single-pixel/non-block-aligned dimensions, deterministic output, and a 67.2% display RGB MSE reduction against an independently seeded 1024 spp reference. Native 4K live profiling completed with denoising on and off.
