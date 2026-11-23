#pragma once
#include <windows.h>
#include <cstdint>
#include <memory>

class DxPresenter {
    struct Resources;
    std::unique_ptr<Resources> resources;
public:
    DxPresenter(HWND parent,int width,int height);
    ~DxPresenter();
    void transfer(const uint32_t* devicePixels);
    void present(RECT viewport);
};
