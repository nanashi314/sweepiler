// The reference's own Win32 window: a swap chain on the lab's D3D device and an ImGui context.
#include "PixelDocs.h"
#include "LabCommon.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
namespace lab {
namespace {
LRESULT CALLBACK docsProc(HWND h,UINT m,WPARAM w,LPARAM l) {
    auto* self=reinterpret_cast<PixelDocs*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(self) return static_cast<LRESULT>(self->handle(h,m,w,l));
    return DefWindowProcW(h,m,w,l);
}
}
PixelDocs::~PixelDocs() {if(window_) close();}
void PixelDocs::open() {
    if(window_) {ShowWindow(static_cast<HWND>(window_),SW_SHOWNORMAL); SetForegroundWindow(static_cast<HWND>(window_)); return;}
    HINSTANCE instance=GetModuleHandleW(nullptr);
    static bool registered=false;
    if(!registered) {
        WNDCLASSEXW wc{sizeof(wc)}; wc.style=CS_CLASSDC; wc.lpfnWndProc=docsProc; wc.hInstance=instance; wc.lpszClassName=L"MineCDocsWindow"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
        RegisterClassExW(&wc); registered=true;
    }
    HWND h=CreateWindowExW(0,L"MineCDocsWindow",L"MineC Reference",WS_OVERLAPPEDWINDOW,160,90,1000,720,nullptr,nullptr,instance,nullptr);
    if(!h) return;
    SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(this));
    // A swap chain for this window on the lab's device.
    IDXGIDevice* dxgi{}; IDXGIAdapter* adapter{}; IDXGIFactory* factory{};
    device_->QueryInterface(IID_PPV_ARGS(&dxgi)); dxgi->GetAdapter(&adapter); adapter->GetParent(IID_PPV_ARGS(&factory));
    DXGI_SWAP_CHAIN_DESC sd{}; sd.BufferCount=2; sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow=h; sd.SampleDesc.Count=1; sd.Windowed=TRUE; sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* swap{}; factory->CreateSwapChain(device_,&sd,&swap);
    factory->Release(); adapter->Release(); dxgi->Release();
    if(!swap) {DestroyWindow(h); return;}
    ID3D11Texture2D* back{}; swap->GetBuffer(0,IID_PPV_ARGS(&back)); ID3D11RenderTargetView* rtv{}; device_->CreateRenderTargetView(back,nullptr,&rtv); back->Release();
    window_=h; swapChain_=swap; target_=rtv;
    // Its own ImGui context with the Japanese font for the text.
    ImGuiContext* previous=ImGui::GetCurrentContext();
    imgui_=ImGui::CreateContext(); ImGui::SetCurrentContext(imgui_);
    auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.Fonts->AddFontDefault();
    for(const char* f:{"C:/Windows/Fonts/BIZ-UDGothicR.ttc","C:/Windows/Fonts/YuGothM.ttc","C:/Windows/Fonts/meiryo.ttc"})
        if(GetFileAttributesA(f)!=INVALID_FILE_ATTRIBUTES) {japanese_=io.Fonts->AddFontFromFileTTF(f,18.0f,nullptr,io.Fonts->GetGlyphRangesJapanese()); break;}
    io.UserData=japanese_;
    ImGui::StyleColorsDark(); ImGui::GetStyle().WindowPadding={0,0}; ImGui::GetStyle().WindowBorderSize=0;
    ImGui_ImplWin32_Init(h); ImGui_ImplDX11_Init(device_,context_);
    ImGui::SetCurrentContext(previous);
    ShowWindow(h,SW_SHOWNORMAL); UpdateWindow(h);
}
long long PixelDocs::handle(void* hwnd,unsigned msg,unsigned long long w,long long l) {
    HWND h=static_cast<HWND>(hwnd);
    if(imgui_) {
        ImGuiContext* previous=ImGui::GetCurrentContext(); ImGui::SetCurrentContext(imgui_);
        LRESULT r=ImGui_ImplWin32_WndProcHandler(h,msg,static_cast<WPARAM>(w),static_cast<LPARAM>(l));
        ImGui::SetCurrentContext(previous);
        if(r) return r;
    }
    switch(msg) {
    case WM_SIZE: if(w!=SIZE_MINIMIZED) {resizeW_=LOWORD(l); resizeH_=HIWORD(l);} return 0;
    case WM_CLOSE: closing_=true; return 0;
    case WM_SYSCOMMAND: if((w&0xfff0)==SC_KEYMENU) return 0; break;
    }
    return DefWindowProcW(h,msg,static_cast<WPARAM>(w),static_cast<LPARAM>(l));
}
void PixelDocs::close() {
    ImGuiContext* previous=ImGui::GetCurrentContext();
    if(imgui_) {ImGui::SetCurrentContext(imgui_); ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext(imgui_); imgui_=nullptr;}
    ImGui::SetCurrentContext(previous==imgui_?nullptr:previous);
    if(target_) static_cast<ID3D11RenderTargetView*>(target_)->Release();
    if(swapChain_) static_cast<IDXGISwapChain*>(swapChain_)->Release();
    target_=swapChain_=nullptr;
    if(window_) {SetWindowLongPtrW(static_cast<HWND>(window_),GWLP_USERDATA,0); DestroyWindow(static_cast<HWND>(window_));}
    window_=nullptr; closing_=false; japanese_=nullptr;
}
void PixelDocs::render() {
    if(!window_) return;
    if(closing_) {close(); return;}
    ImGuiContext* previous=ImGui::GetCurrentContext(); ImGui::SetCurrentContext(imgui_);
    auto* swap=static_cast<IDXGISwapChain*>(swapChain_);
    if(resizeW_) {
        static_cast<ID3D11RenderTargetView*>(target_)->Release();
        swap->ResizeBuffers(0,resizeW_,resizeH_,DXGI_FORMAT_UNKNOWN,0); resizeW_=resizeH_=0;
        ID3D11Texture2D* back{}; swap->GetBuffer(0,IID_PPV_ARGS(&back)); ID3D11RenderTargetView* rtv{}; device_->CreateRenderTargetView(back,nullptr,&rtv); back->Release(); target_=rtv;
    }
    ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
    ui(ImGui::GetIO().DisplaySize.x,ImGui::GetIO().DisplaySize.y);
    ImGui::Render();
    auto* rtv=static_cast<ID3D11RenderTargetView*>(target_);
    const float clear[4]={0,0,0,1};
    context_->OMSetRenderTargets(1,&rtv,nullptr); context_->ClearRenderTargetView(rtv,clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    if(!capturePath_.empty()) {saveBackBuffer(device_,context_,swap,capturePath_); capturePath_.clear();}
    swap->Present(0,0);
    ImGui::SetCurrentContext(previous);
}
}
