#include "app.h"
#include "room_scene.h"
#include "gpu_api.h"
#include "denoiser.h"
#include "camera_refinement.h"
#include <iomanip>
#include <iostream>
#include <iterator>
// CPU viewer: GPU kernels are accessed through gpu_api.h and Denoiser.
#include <windows.h>
#include <windowsx.h>
#include <chrono>
#include <memory>
#include <sstream>
#include <thread>
#include "frame_profiler.h"
#include "dx_presenter.h"

namespace viewer {
enum Action { Forward, Back, Left, Right, Up, Down, TurnLeft, TurnRight, LookUp, LookDown, Reset, Replay, ActionCount };
struct Button { RECT rect; const char* label; Action action; };
struct State {
    HWND window=nullptr;
    int width=960,height=640,renderWidth=0,renderHeight=0,samples=0;
    bool running=true,focused=true,looking=false,dirty=true,minimized=false,denoise=true;
    bool replayRose=false;
    bool keys[256]{};
    int held=-1;
    POINT lastMouse{};
    Vec3 position{0,1.65f,2.8f};
    float yaw=0,pitch=RoomCameraPitch;
    std::vector<Button> buttons;
    std::vector<uint32_t> pixels;
    std::string status="Starting CUDA renderer...";
    HFONT font=nullptr;
    RECT imageRect() const {
        int bottom=buttons.empty()?height:buttons.front().rect.top-8;
        int available=std::max(1,bottom-54);
        float fit=std::min(float(width)/renderWidth,float(available)/renderHeight);
        int w=std::max(1,int(renderWidth*fit)),h=std::max(1,int(renderHeight*fit));
        int x=(width-w)/2,y=54+(available-h)/2;
        return {x,y,x+w,y+h};
    }
    void reset() { position={0,1.65f,2.8f};yaw=0;pitch=RoomCameraPitch;dirty=true; }
    void layout() {
        buttons.clear();
        const char* labels[]={"Forward W","Back S","Left A","Right D","Up E","Down Q",
                              "Look left","Look right","Look up","Look down","Reset R","Replay P"};
        int gap=5;
        int buttonWidth=std::clamp((width-20)/ActionCount-gap,72,88);
        int columns=std::clamp((width-20)/(buttonWidth+gap),1,int(ActionCount));
        int rows=(ActionCount+columns-1)/columns;
        for (int i=0;i<ActionCount;++i) {
            int x=10+(i%columns)*(buttonWidth+gap),y=height-12-rows*33+(i/columns)*33;
            buttons.push_back({{x,y,x+buttonWidth,y+28},labels[i],Action(i)});
        }
    }
    int buttonAt(int x,int y) const {
        POINT p{x,y};
        for (int i=0;i<int(buttons.size());++i) if (PtInRect(&buttons[i].rect,p)) return i;
        return -1;
    }
};

void paint(State& s,HDC dc) {
    constexpr int headerHeight=54;
    int footerTop=s.buttons.empty()?s.height:s.buttons.front().rect.top-8;
    RECT viewport{0,headerHeight,s.width,footerTop};
    HBRUSH black=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    if (!s.pixels.empty()) {
        struct { BITMAPINFOHEADER header; DWORD masks[3]; } info{{},{0xff,0xff00,0xff0000}};
        info.header.biSize=sizeof(BITMAPINFOHEADER);
        info.header.biWidth=s.renderWidth;info.header.biHeight=-s.renderHeight;
        info.header.biPlanes=1;info.header.biBitCount=32;info.header.biCompression=BI_BITFIELDS;
        RECT image=s.imageRect();
        int x=image.left,y=image.top,displayWidth=image.right-x,displayHeight=image.bottom-y;
        // Paint only the letterbox bars so presenting a frame never clears the old image.
        const RECT bars[]={{0,headerHeight,s.width,y},{0,y,x,y+displayHeight},
                           {x+displayWidth,y,s.width,y+displayHeight},{0,y+displayHeight,s.width,footerTop}};
        for (const auto& bar : bars) FillRect(dc,&bar,black);
        SetStretchBltMode(dc,HALFTONE);
        SetBrushOrgEx(dc,0,0,nullptr);
        StretchDIBits(dc,x,y,
                      displayWidth,displayHeight,0,0,s.renderWidth,s.renderHeight,
                      s.pixels.data(),reinterpret_cast<const BITMAPINFO*>(&info),DIB_RGB_COLORS,SRCCOPY);
    } else FillRect(dc,&viewport,black);
    HGDIOBJ oldFont=SelectObject(dc,s.font);
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(240,244,248));
    HBRUSH panel=CreateSolidBrush(RGB(22,27,34));
    RECT header{0,0,s.width,headerHeight};FillRect(dc,&header,panel);
    RECT title{12,6,s.width-10,27};DrawTextA(dc,s.status.c_str(),-1,&title,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX);
    RECT help{12,29,s.width-10,51};
    DrawTextA(dc,"WASD move | Q/E down/up | Arrows look | Right mouse look | Shift faster | N denoise | Esc exit",-1,&help,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX);
    if (!s.buttons.empty()) {
        RECT footer{0,footerTop,s.width,s.height};FillRect(dc,&footer,panel);
    }
    DeleteObject(panel);
    for (int i=0;i<int(s.buttons.size());++i) {
        const Button& b=s.buttons[i];
        HBRUSH brush=CreateSolidBrush(i==s.held?RGB(38,129,169):RGB(53,64,78));
        FillRect(dc,&b.rect,brush);DeleteObject(brush);
        RECT text=b.rect;DrawTextA(dc,b.label,-1,&text,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }
    SelectObject(dc,oldFont);
}

LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM w,LPARAM l) {
    State* s=reinterpret_cast<State*>(GetWindowLongPtr(window,GWLP_USERDATA));
    if (message==WM_NCCREATE) {
        s=static_cast<State*>(reinterpret_cast<CREATESTRUCT*>(l)->lpCreateParams);
        s->window=window;SetWindowLongPtr(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));
    }
    if (!s) return DefWindowProc(window,message,w,l);
    switch (message) {
    case WM_CLOSE: s->running=false;DestroyWindow(window);return 0;
    case WM_DESTROY: s->window=nullptr;s->running=false;PostQuitMessage(0);return 0;
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:
        s->minimized=w==SIZE_MINIMIZED;
        s->width=std::max(1,int(LOWORD(l)));s->height=std::max(1,int(HIWORD(l)));
        s->layout();return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={640,480};return 0;
    case WM_KILLFOCUS:
        s->focused=false;std::fill(std::begin(s->keys),std::end(s->keys),false);
        s->held=-1;s->looking=false;ReleaseCapture();return 0;
    case WM_SETFOCUS: s->focused=true;return 0;
    case WM_KEYDOWN:
        if (w<256) s->keys[w]=true;
        if (w==VK_ESCAPE) SendMessage(window,WM_CLOSE,0,0);
        if (w=='R') s->reset();
        if (w=='N' && !(l & (1LL<<30))) s->denoise=!s->denoise;
        if (w=='P' && !(l & (1LL<<30))) s->replayRose=true;
        return 0;
    case WM_KEYUP: if (w<256) s->keys[w]=false;return 0;
    case WM_LBUTTONDOWN:
        SetFocus(window);s->held=s->buttonAt(GET_X_LPARAM(l),GET_Y_LPARAM(l));
        if (s->held>=0) {
            SetCapture(window);
            if (s->buttons[s->held].action==Reset) s->reset();
            if (s->buttons[s->held].action==Replay) s->replayRose=true;
        }
        return 0;
    case WM_LBUTTONUP: s->held=-1;ReleaseCapture();return 0;
    case WM_RBUTTONDOWN:
        SetFocus(window);s->looking=true;s->lastMouse={GET_X_LPARAM(l),GET_Y_LPARAM(l)};
        SetCapture(window);return 0;
    case WM_RBUTTONUP: s->looking=false;ReleaseCapture();return 0;
    case WM_CAPTURECHANGED: s->held=-1;s->looking=false;return 0;
    case WM_MOUSEMOVE:
        if (s->looking) {
            POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
            s->yaw+=(p.x-s->lastMouse.x)*0.004f;
            s->pitch=std::clamp(s->pitch-(p.y-s->lastMouse.y)*0.004f,-1.5f,1.5f);
            s->lastMouse=p;s->dirty=true;
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps);paint(*s,dc);EndPaint(window,&ps);return 0;
    }
    }
    return DefWindowProc(window,message,w,l);
}

bool updateCamera(State& s,float dt) {
    auto active=[&](Action action,int key) {
        return s.keys[key] || (s.held>=0 && s.buttons[s.held].action==action);
    };
    float f=float(active(Forward,'W'))-float(active(Back,'S'));
    float r=float(active(Right,'D'))-float(active(Left,'A'));
    float u=float(active(Up,'E'))-float(active(Down,'Q'));
    float turn=float(active(TurnRight,VK_RIGHT))-float(active(TurnLeft,VK_LEFT));
    float tilt=float(active(LookUp,VK_UP))-float(active(LookDown,VK_DOWN));
    if (f==0 && r==0 && u==0 && turn==0 && tilt==0) return false;
    s.yaw+=turn*dt*1.4f;s.pitch=std::clamp(s.pitch+tilt*dt*1.4f,-1.5f,1.5f);
    Vec3 forward{std::sin(s.yaw)*std::cos(s.pitch),std::sin(s.pitch),-std::cos(s.yaw)*std::cos(s.pitch)};
    Vec3 right{std::cos(s.yaw),0,std::sin(s.yaw)};
    Vec3 movement=forward*f+right*r+Vec3(0,u,0);
    if (dot(movement,movement)>0) s.position+=unit(movement)*(dt*(s.keys[VK_SHIFT]?4.5f:1.8f));
    // Free camera: intentionally allows exploration outside the room and through objects.
    return true;
}

struct WindowLifetime {
    State& state;
    ~WindowLifetime() {
        if (state.window) DestroyWindow(state.window);
        if (state.font) DeleteObject(state.font);
    }
};
} // namespace viewer

int runLiveViewer(RoomScene& room,const Options& options) {
    using namespace viewer;
    SetProcessDPIAware();
    State state;
    state.denoise=options.denoise;
    WindowLifetime lifetime{state};
    HINSTANCE instance=GetModuleHandle(nullptr);
    WNDCLASSA wc{};wc.lpfnWndProc=procedure;wc.hInstance=instance;
    wc.lpszClassName="CudaRoomLiveViewer";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    if (!RegisterClassA(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
        throw std::runtime_error("Cannot register viewer window");
    state.font=CreateFontA(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,"Segoe UI");
    RECT bounds{0,0,std::max(640,options.width),std::max(480,options.height)};
    AdjustWindowRect(&bounds,WS_OVERLAPPEDWINDOW,FALSE);
    RECT workArea{};
    if (!SystemParametersInfo(SPI_GETWORKAREA,0,&workArea,0))
        throw std::runtime_error("Cannot query desktop work area");
    int workWidth=workArea.right-workArea.left,workHeight=workArea.bottom-workArea.top;
    bool maximize=bounds.right-bounds.left>workWidth || bounds.bottom-bounds.top>workHeight;
    HWND window=CreateWindowExA(0,wc.lpszClassName,"CUDA Room - Live Path Tracing",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,std::min(int(bounds.right-bounds.left),workWidth),
        std::min(int(bounds.bottom-bounds.top),workHeight),
        nullptr,nullptr,instance,&state);
    if (!window) throw std::runtime_error("Cannot create viewer window");
    ShowWindow(window,maximize?SW_SHOWMAXIMIZED:SW_SHOW);UpdateWindow(window);
    std::unique_ptr<DeviceBuffer<Vec3>> accumulation;
    std::unique_ptr<DeviceBuffer<uint32_t>> display;
    std::unique_ptr<Denoiser> denoiser;
    std::unique_ptr<DxPresenter> presenter;
    try {
        presenter=std::make_unique<DxPresenter>(window,options.width,options.height);
        std::cout<<"Display: CUDA / Direct3D 11\n";
    } catch (const std::exception& e) {
        cudaGetLastError();
        std::cerr<<"Using GDI display: "<<e.what()<<'\n';
    }
    bool guidesDirty=true;
    using Clock=std::chrono::steady_clock;
    auto previous=Clock::now(),statsStart=previous;
    auto animationStart=previous;
    bool playing=!options.sceneTime.has_value();
    int frames=0,statsFrames=0;
    float fps=0;
    FrameProfiler profiler(options.profile);
    CameraRefinement refinement;
    uint32_t sampleSequence=0;
    while (state.running) {
        auto frameStart=Clock::now();
        MSG message;
        while (PeekMessage(&message,nullptr,0,0,PM_REMOVE)) {
            if (message.message==WM_QUIT) state.running=false;
            TranslateMessage(&message);DispatchMessage(&message);
        }
        if (!state.running) break;
        double intervalMs=std::chrono::duration<double,std::milli>(frameStart-previous).count();
        float dt=std::min(0.05f,float(intervalMs*0.001));previous=frameStart;
        if (state.minimized) { std::this_thread::sleep_for(std::chrono::milliseconds(30));continue; }
        // Render at the requested resolution even when the window is smaller.
        int width=options.width,height=options.height;
        if (width!=state.renderWidth || height!=state.renderHeight) {
            accumulation=std::make_unique<DeviceBuffer<Vec3>>(size_t(width)*height);
            display=std::make_unique<DeviceBuffer<uint32_t>>(size_t(width)*height);
            // Release the old buffers before allocating for the new resolution.
            denoiser.reset();guidesDirty=true;
            state.renderWidth=width;state.renderHeight=height;
            if (!presenter) state.pixels.resize(size_t(width)*height);
            state.dirty=true;
        }
        bool replayed=state.replayRose;
        if (replayed) {
            animationStart=frameStart;
            playing=true;
            state.replayRose=false;
        }
        float sceneTime=std::chrono::duration<float>(frameStart-animationStart).count();
        if (playing) room.setTime(sceneTime);
        bool moved=state.focused && updateCamera(state,dt);
        bool cameraChanged=moved || state.dirty;
        guidesDirty=guidesDirty || playing || cameraChanged;
        if (playing || cameraChanged || state.samples>1000000000) {
            check(cudaMemset(accumulation->data,0,size_t(width)*height*sizeof(Vec3)),"Reset live accumulation");
            state.samples=0;state.dirty=false;
        }
        Camera camera=makeCamera(state.position,state.yaw,state.pitch,float(width)/height);
        bool reuseHistory=playing && !cameraChanged && state.denoise;
        profiler.mark(0);
        if (state.denoise) {
            if (!denoiser) denoiser=std::make_unique<Denoiser>(width,height);
            if (reuseHistory && denoiser->preparePlayback()) guidesDirty=true;
            if (guidesDirty) { denoiser->updateGuides(room.view,camera);guidesDirty=false; }
        }
        if (denoiser && (replayed || cameraChanged || !state.denoise))
            denoiser->resetHistory();
        profiler.mark(1);
        // Draw fresh samples where history is safe. Motion-sensitive rays keep the same pattern.
        int batch=refinement.sampleBudget(cameraChanged,state.denoise,options.liveSpp);
        const uint8_t* historyMask=reuseHistory?denoiser->historyMask():nullptr;
        uint32_t sampleOffset=sampleSequence;
        sampleSequence+=uint32_t(batch); // Refinement budgets vary; never repeat stable-pixel samples.
        launchRender(room.view,camera,accumulation->data,width,height,state.samples,batch,
                              options.bounces,options.seed,historyMask,sampleOffset);
        check(cudaGetLastError(),"Launch live path tracing");state.samples+=batch;
        profiler.mark(2);
        DenoiseMode mode=reuseHistory?DenoiseMode::Playback
                                   : playing?DenoiseMode::Spatial:DenoiseMode::Settling;
        const Vec3* result=state.denoise?denoiser->apply(accumulation->data,state.samples,mode):accumulation->data;
        profiler.mark(3);
        launchToneMap(result,display->data,width*height,state.samples,options.exposure);
        check(cudaGetLastError(),"Launch live tone mapping");
        profiler.mark(4);
        auto copyStart=Clock::now();
        if (presenter) {
            try { presenter->transfer(display->data); }
            catch (const std::exception& e) {
                std::cerr<<"Switching to GDI display: "<<e.what()<<'\n';
                presenter.reset();cudaGetLastError();
            }
        }
        if (!presenter) {
            state.pixels.resize(size_t(width)*height);
            check(cudaMemcpy(state.pixels.data(),display->data,size_t(width)*height*sizeof(uint32_t),cudaMemcpyDeviceToHost),"Download live frame");
        }
        double copyMs=std::chrono::duration<double,std::milli>(Clock::now()-copyStart).count();
        profiler.mark(5);
        ++frames;++statsFrames;
        float statsTime=std::chrono::duration<float>(Clock::now()-statsStart).count();
        if (statsTime>=0.4f) { fps=statsFrames/statsTime;statsFrames=0;statsStart=Clock::now(); }
        std::ostringstream status;status<<"CUDA live | "<<std::fixed<<std::setprecision(0)<<fps<<" FPS | "
            <<width<<'x'<<height<<" | "<<state.samples<<" spp | Denoise "<<(state.denoise?"ON":"OFF")<<" | Camera "<<std::setprecision(2)
            <<state.position.x<<", "<<state.position.y<<", "<<state.position.z;
        if (refinement.active() && !cameraChanged && state.denoise) status<<" | Refining";
        state.status=status.str();
        auto presentStart=Clock::now();
        if (presenter) {
            try { presenter->present(state.imageRect()); }
            catch (const std::exception& e) {
                std::cerr<<"Switching to GDI display: "<<e.what()<<'\n';
                presenter.reset();
            }
        }
        InvalidateRect(window,nullptr,FALSE);UpdateWindow(window);
        auto presentEnd=Clock::now();
        // Bound the live queue to one CUDA frame, including when presentation is asynchronous.
        if (presenter) check(cudaStreamSynchronize(nullptr),"Complete live GPU frame");
        profiler.finish(std::chrono::duration<double,std::milli>(Clock::now()-frameStart).count(),
                        std::chrono::duration<double,std::milli>(presentEnd-presentStart).count(),copyMs,intervalMs);
        if (options.frames && frames>=options.frames) break;
        std::this_thread::sleep_until(frameStart+std::chrono::milliseconds(16));
    }
    profiler.report();
    std::cout<<"Live viewer closed after "<<frames<<" frames; render "<<state.renderWidth<<'x'
             <<state.renderHeight<<"; camera "<<state.position.x<<", "
             <<state.position.y<<", "<<state.position.z<<"; accumulated "<<state.samples<<" spp.\n";
    return 0;
}
