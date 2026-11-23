cmake_minimum_required(VERSION 3.23)
# Override with -DCMAKE_CUDA_ARCHITECTURES=89, for example, to target another GPU.
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES 75)
endif()

project(cuda_room_path_tracer LANGUAGES CXX CUDA)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(petal_data "${CMAKE_CURRENT_BINARY_DIR}/scene_texture_data.cpp")
add_custom_command(OUTPUT "${petal_data}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_textures.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/data/rose-petal.png"
            "${CMAKE_CURRENT_SOURCE_DIR}/data/oak-floor.png"
            "${CMAKE_CURRENT_SOURCE_DIR}/data/lime-plaster.png" "${petal_data}"
    DEPENDS tools/embed_textures.py data/rose-petal.png data/oak-floor.png data/lime-plaster.png
    VERBATIM)
find_package(CUDAToolkit REQUIRED)
add_library(renderer_core STATIC
    src/scene_builder.cpp src/rose.cpp src/aquarium_scene.cpp
    src/bvh_builder.cpp src/scene_upload.cpp src/water_model.cpp
    src/render.cu src/denoiser.cu src/water.cu src/display.cu
    "${petal_data}")
target_include_directories(renderer_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(renderer_core PUBLIC CUDA::cudart)
set_target_properties(renderer_core PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES
    CUDA_STANDARD 17 CUDA_STANDARD_REQUIRED YES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_compile_options(renderer_core PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:--use_fast_math>)
add_executable(path_tracer src/main.cpp src/options.cpp src/image_io.cpp)
target_link_libraries(path_tracer PRIVATE renderer_core)
if(WIN32)
    target_sources(path_tracer PRIVATE src/live_viewer.cpp src/dx_presenter.cpp)
    target_include_directories(path_tracer PRIVATE ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES})
    target_link_libraries(path_tracer PRIVATE user32 gdi32 d3d11 dxgi)
    target_compile_definitions(renderer_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT path_tracer)
    set_target_properties(path_tracer PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(verify_sampling tests/verify_sampling.cpp)
    set_target_properties(verify_sampling PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
    add_test(NAME adaptive_sampling COMMAND verify_sampling)
    add_executable(verify_water tests/verify_water.cpp src/water_model.cpp)
    target_include_directories(verify_water PRIVATE src)
    add_test(NAME water_model COMMAND verify_water)
    add_executable(verify_render_state tests/verify_render_state.cpp)
    target_link_libraries(verify_render_state PRIVATE renderer_core)
    add_test(NAME camera_denoising COMMAND verify_render_state)
    add_test(NAME texture_mipmaps COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/verify_textures.py")
    add_test(NAME acceleration
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/verify_acceleration.py" "$<TARGET_FILE:path_tracer>")
    add_test(NAME denoising
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/verify_denoising.py" "$<TARGET_FILE:path_tracer>")
    # CUDA render tests run serially to isolate their timing and GPU memory use.
    set_tests_properties(acceleration denoising camera_denoising PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)
endif()
