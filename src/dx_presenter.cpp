#include "dx_presenter.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <cuda_d3d11_interop.h>
#include <cuda_runtime_api.h>
#include <wrl/client.h>
#include <cstring>
#include <sstream>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
namespace {
void dxCheck(HRESULT hr,const char* operation) {
    if (FAILED(hr)) {
        std::ostringstream message;
        message<<operation<<" (HRESULT 0x"<<std::hex<<unsigned(hr)<<")";
        throw std::runtime_error(message.str());
    }
}
void cudaCheck(cudaError_t code,const char* operation) {
    if (code!=cudaSuccess) throw std::runtime_error(std::string(operation)+": "+cudaGetErrorString(code));
}
LRESULT CALLBACK viewportProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    if (message==WM_NCHITTEST) return HTTRANSPARENT;
    if (message==WM_ERASEBKGND) return 1;
    return DefWindowProc(window,message,w,l);
}
}

struct DxPresenter::Resources {
    HWND window=nullptr;
    RECT layout{};
    int width=0,height=0;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain1> swap;
    ComPtr<ID3D11Texture2D> texture,back;
    cudaGraphicsResource* resource=nullptr;

    ~Resources() {
        if (resource) cudaGraphicsUnregisterResource(resource);
        back.Reset();swap.Reset();
        if (window && IsWindow(window)) DestroyWindow(window);
    }
};

DxPresenter::~DxPresenter()=default;

DxPresenter::DxPresenter(HWND parent,int width,int height) : resources(std::make_unique<Resources>()) {
    auto& r=*resources;
    r.width=width;r.height=height;
    int ordinal;
    cudaDeviceProp properties{};
    cudaCheck(cudaGetDevice(&ordinal),"Get CUDA adapter");
    cudaCheck(cudaGetDeviceProperties(&properties,ordinal),"Get CUDA adapter LUID");
    ComPtr<IDXGIFactory2> factory;
    dxCheck(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())),"Create DXGI factory");
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i=0;;++i) {
        HRESULT hr=factory->EnumAdapters1(i,adapter.ReleaseAndGetAddressOf());
        if (hr==DXGI_ERROR_NOT_FOUND) throw std::runtime_error("No DXGI adapter matching the CUDA device");
        dxCheck(hr,"Enumerate adapters");
        DXGI_ADAPTER_DESC1 desc{};
        dxCheck(adapter->GetDesc1(&desc),"Read adapter description");
        if (!std::memcmp(properties.luid,&desc.AdapterLuid,sizeof(desc.AdapterLuid))) break;
    }
    dxCheck(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
                             r.device.GetAddressOf(),nullptr,r.context.GetAddressOf()),"Create D3D11 device");
    WNDCLASSW wc{};
    wc.lpfnWndProc=viewportProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"CudaFrameViewport";
    if (!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
        throw std::runtime_error("Register D3D viewport");
    r.window=CreateWindowExW(0,wc.lpszClassName,L"",WS_CHILD|WS_DISABLED,0,0,1,1,
                             parent,nullptr,wc.hInstance,nullptr);
    if (!r.window) throw std::runtime_error("Create D3D viewport");
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width=width;sd.Height=height;sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;
    sd.Scaling=DXGI_SCALING_STRETCH;sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
    dxCheck(factory->CreateSwapChainForHwnd(r.device.Get(),r.window,&sd,nullptr,nullptr,r.swap.GetAddressOf()),
            "Create viewport swap chain");
    dxCheck(factory->MakeWindowAssociation(parent,DXGI_MWA_NO_ALT_ENTER),"Set window association");
    dxCheck(r.swap->GetBuffer(0,IID_PPV_ARGS(r.back.GetAddressOf())),"Get display buffer");
    D3D11_TEXTURE2D_DESC td{};
    td.Width=width;td.Height=height;td.MipLevels=1;td.ArraySize=1;td.Format=sd.Format;
    td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    dxCheck(r.device->CreateTexture2D(&td,nullptr,r.texture.GetAddressOf()),"Create CUDA display texture");
    cudaCheck(cudaGraphicsD3D11RegisterResource(&r.resource,r.texture.Get(),cudaGraphicsRegisterFlagsNone),
              "Register display texture");
    cudaCheck(cudaGraphicsResourceSetMapFlags(r.resource,cudaGraphicsMapFlagsWriteDiscard),"Set texture map flags");
}

void DxPresenter::transfer(const uint32_t* devicePixels) {
    auto& r=*resources;
    cudaCheck(cudaGraphicsMapResources(1,&r.resource),"Map display texture");
    cudaArray_t array=nullptr;
    cudaError_t result=cudaGraphicsSubResourceGetMappedArray(&array,r.resource,0,0);
    size_t pitch=size_t(r.width)*sizeof(uint32_t);
    if (result==cudaSuccess)
        result=cudaMemcpy2DToArrayAsync(array,0,0,devicePixels,pitch,pitch,r.height,cudaMemcpyDeviceToDevice);
    cudaError_t unmap=cudaGraphicsUnmapResources(1,&r.resource);
    cudaCheck(result,"Write display texture");
    cudaCheck(unmap,"Unmap display texture");
}

void DxPresenter::present(RECT viewport) {
    auto& r=*resources;
    if (!EqualRect(&r.layout,&viewport)) {
        SetWindowPos(r.window,nullptr,viewport.left,viewport.top,viewport.right-viewport.left,
                     viewport.bottom-viewport.top,SWP_NOACTIVATE|SWP_NOZORDER|SWP_SHOWWINDOW);
        r.layout=viewport;
    }
    r.context->CopyResource(r.back.Get(),r.texture.Get());
    dxCheck(r.swap->Present(0,0),"Present GPU frame");
}
