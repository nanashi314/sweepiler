// sweepiler: the app's window for the pixel UI on top of the machine core, and
// the MineC reference in a second window. Platform code only (Win32, D3D11,
// Dear ImGui); the UI is PixelApp, the reference PixelDocs.
//
//   sweepiler.exe                              interactive
//   sweepiler.exe --smoke=N [--ticks=T] [--capture=PREFIX] [--code=FILE] [--board-zoom=PX] [--doc=TOPIC] [--type=TEXT] [--docs-wheel=N] [--docs-click=X,Y]
//       builds program N, runs T ticks with keys, saves PREFIX-display.bmp,
//       PREFIX-board.bmp, PREFIX-code.bmp and PREFIX-docs.bmp (the reference
//       open at topic TOPIC), prints PASS and exits (1 on failure).
#include "PixelApp.h"
#include "PixelDocs.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
namespace {
ID3D11Device* device{}; ID3D11DeviceContext* context{}; IDXGISwapChain* swapChain{}; ID3D11RenderTargetView* target{};
ImGuiContext* mainContext{};
UINT resizeW{},resizeH{};
void createTarget() {ID3D11Texture2D* back{}; swapChain->GetBuffer(0,IID_PPV_ARGS(&back)); device->CreateRenderTargetView(back,nullptr,&target); back->Release();}
void releaseTarget() {if(target) {target->Release(); target=nullptr;}}
LRESULT CALLBACK windowProc(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(mainContext) {
        ImGuiContext* previous=ImGui::GetCurrentContext(); ImGui::SetCurrentContext(mainContext);
        LRESULT r=ImGui_ImplWin32_WndProcHandler(h,m,w,l);
        ImGui::SetCurrentContext(previous);
        if(r) return 1;
    }
    switch(m) {
    case WM_SIZE: if(w!=SIZE_MINIMIZED) {resizeW=LOWORD(l); resizeH=HIWORD(l);} return 0;
    case WM_SYSCOMMAND: if((w&0xfff0)==SC_KEYMENU) return 0; break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
std::string option(const std::wstring& command,const wchar_t* name) {
    auto at=command.find(name); if(at==std::wstring::npos) return {};
    auto start=at+wcslen(name),end=command.find(L' ',start);
    std::wstring v=command.substr(start,end==std::wstring::npos?std::wstring::npos:end-start);
    std::string out; for(wchar_t c:v) out+=static_cast<char>(c); return out;
}
// Smoke results: to the console, and to PREFIX-result.txt when --capture is given.
std::string resultFile;
void report(const char* format,...) {
    char line[512]; va_list args; va_start(args,format); std::vsnprintf(line,sizeof line,format,args); va_end(args);
    std::fputs(line,stdout); std::fflush(stdout);
    if(!resultFile.empty()) if(FILE* f=nullptr; fopen_s(&f,resultFile.c_str(),"a")==0 && f) {std::fputs(line,f); std::fclose(f);}
}
std::filesystem::path cacheDirectory() {
    char* local=nullptr; size_t n=0; std::filesystem::path base=std::filesystem::temp_directory_path();
    if(_dupenv_s(&local,&n,"LOCALAPPDATA")==0 && local) {base=local; std::free(local);}
    return base/"InfiniteMinesweeper"/"frames";
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    const std::wstring command=GetCommandLineW();
    const bool smoke=command.find(L"--smoke")!=std::wstring::npos;
    const std::string smokeProgram=option(command,L"--smoke="),ticksText=option(command,L"--ticks="),capture=option(command,L"--capture=");
    const std::string codeFile=option(command,L"--code="),boardZoom=option(command,L"--board-zoom="),docTopic=option(command,L"--doc="),typed=option(command,L"--type=");
    const std::string docsWheel=option(command,L"--docs-wheel="),docsClick=option(command,L"--docs-click="),capText=option(command,L"--cap=");
    const int cap=capText.empty()?-1:std::atoi(capText.c_str());
    const int smokeTicks=ticksText.empty()?20:std::atoi(ticksText.c_str());
    if(!capture.empty()) {resultFile=capture+"-result.txt"; std::remove(resultFile.c_str());}
    if(smoke) {AttachConsole(ATTACH_PARENT_PROCESS); FILE* f{}; freopen_s(&f,"CONOUT$","w",stdout);}
    ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW wc{sizeof(wc)}; wc.style=CS_CLASSDC; wc.lpfnWndProc=windowProc; wc.hInstance=instance; wc.lpszClassName=L"MinesweeperUiLab"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    RegisterClassExW(&wc);
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"sweepiler",WS_OVERLAPPEDWINDOW,100,80,1280,800,nullptr,nullptr,instance,nullptr);
    DXGI_SWAP_CHAIN_DESC sd{}; sd.BufferCount=2; sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow=hwnd; sd.SampleDesc.Count=1; sd.Windowed=TRUE; sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0}; D3D_FEATURE_LEVEL got{};
    if(FAILED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,levels,1,D3D11_SDK_VERSION,&sd,&swapChain,&device,&got,&context))
       && FAILED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,levels,1,D3D11_SDK_VERSION,&sd,&swapChain,&device,&got,&context))) return 1;
    createTarget();
    ShowWindow(hwnd,SW_SHOWDEFAULT); UpdateWindow(hwnd);
    IMGUI_CHECKVERSION(); mainContext=ImGui::CreateContext(); ImGui::GetIO().IniFilename=nullptr;
    lab::loadFonts();
    ImGui_ImplWin32_Init(hwnd); ImGui_ImplDX11_Init(device,context);
    int exitCode=0;
    {
        ims::MachineSession session(cacheDirectory());
        lab::PixelApp app(session);
        lab::PixelDocs docs(device,context);
        app.setDocs(&docs);
        // OPEN IN EDITOR in the reference: the sample opens in the lab's editor, which comes to the front.
        docs.openSample=[&](const std::string& name){app.openSample(name); SetForegroundWindow(hwnd);};
        if(!docTopic.empty()) docs.showTopic(static_cast<size_t>(std::atoi(docTopic.c_str())));
        app.activate();
        // Smoke stages: 0 build, 1 wait, 2 tick, 3 capture display, 4 capture board, 5 capture code (and the reference), 6 done.
        // With --code=FILE the program comes from the code editor; a compile error skips to the code capture.
        // --cap=I clicks key cap I with the mouse for each tick (instead of pressing keys) and checks the frame got the key.
        int stage=0,tick=0,settle=0,capPhase=0,capHits=0; auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(15);
        bool running=true;
        while(running) {
            MSG msg; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&msg); DispatchMessageW(&msg); if(msg.message==WM_QUIT) running=false;}
            if(!running) break;
            ImGui::SetCurrentContext(mainContext);
            if(resizeW) {releaseTarget(); swapChain->ResizeBuffers(0,resizeW,resizeH,DXGI_FORMAT_UNKNOWN,0); resizeW=resizeH=0; createTarget();}
            ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
            const auto& io=ImGui::GetIO(); app.frame(io.DisplaySize.x,io.DisplaySize.y);
            ImGui::Render();
            const float clear[4]={0,0,0,1};
            context->OMSetRenderTargets(1,&target,nullptr); context->ClearRenderTargetView(target,clear);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            bool captureNow=false;
            if(smoke) {
                auto s=session.status();
                if(!s.error.empty() || std::chrono::steady_clock::now()>deadline) {report("FAIL: %s\n",s.error.empty()?"timed out":s.error.c_str()); exitCode=1; running=false;}
                else if(stage==0) {
                    if(!codeFile.empty()) {stage=app.runCodeFile(codeFile)?1:5; settle=0;}
                    else {app.choose(static_cast<size_t>(smokeProgram.empty()?0:std::atoi(smokeProgram.c_str()))); app.buildChosen(); stage=1;}
                }
                else if(stage==1 && s.ready) stage=2;
                else if(stage==2) {
                    if(cap>=0 && tick>0 && s.ticks==tick && capPhase==5) {
                        // The tick just made: release the cap and check the key reached the frame.
                        ImVec2 c=app.capCenter(static_cast<size_t>(cap)); PostMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(static_cast<int>(c.x),static_cast<int>(c.y)));
                        if(static_cast<size_t>(cap)<s.lastKeys.size() && s.lastKeys[static_cast<size_t>(cap)]) ++capHits;
                        capPhase=0;
                    }
                    if(s.ticks==tick && (cap<0 || capPhase==0)) {
                        if(tick>=smokeTicks) {
                            if(cap>=0) report("%s: key cap %d clicked for %d ticks, %d frames got the key\n",capHits==tick?"PASS":"FAIL",cap,tick,capHits);
                            if(cap>=0 && capHits!=tick) exitCode=1;
                            stage=3; app.showTab(0); settle=0;
                        } else if(cap>=0 && app.capCenter(static_cast<size_t>(cap)).x>=0) {
                            ImVec2 c=app.capCenter(static_cast<size_t>(cap)); LPARAM p=MAKELPARAM(static_cast<int>(c.x),static_cast<int>(c.y));
                            PostMessageW(hwnd,WM_MOUSEMOVE,0,p); PostMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,p); capPhase=1;
                        } else {if(!s.keys.empty() && tick%3) session.pressKey(static_cast<size_t>(tick*7)%s.keys.size()); session.step(); ++tick;}
                    } else if(cap>=0 && capPhase>=1 && capPhase<5) {
                        // Let a few frames see the cap held (a real click lasts longer), then ask for the tick.
                        if(++capPhase==5) {session.step(); ++tick;}
                    }
                } else if(stage==3 && ++settle>3) captureNow=true;
                else if(stage==4 && ++settle>3) captureNow=true;
                else if(stage==5 && ++settle>9) captureNow=true;
                // --type: text typed into the editor before the code capture ('~' stands for a new line, '_' for a space).
                // --doc opens the reference; --docs-wheel / --docs-click send it mouse messages (client x,y).
                if(stage==5 && settle==1 && !docTopic.empty()) docs.open();
                if(stage==5 && settle==1 && command.find(L"--samples")!=std::wstring::npos) app.showSamples();
                // --docs-short makes the reference window short, so its page has to scroll.
                if(stage==5 && settle==2 && docs.isOpen() && command.find(L"--docs-short")!=std::wstring::npos) SetWindowPos(static_cast<HWND>(docs.window()),nullptr,0,0,1000,380,SWP_NOMOVE|SWP_NOZORDER);
                if(stage==5 && (settle==3 || settle==4) && docs.isOpen()) {
                    HWND dw=static_cast<HWND>(docs.window()); RECT rc{}; GetClientRect(dw,&rc);
                    if(settle==3 && !docsWheel.empty()) {
                        LPARAM inside=MAKELPARAM(rc.right*2/3,rc.bottom/2); POINT sp{rc.right*2/3,rc.bottom/2}; ClientToScreen(dw,&sp);
                        PostMessageW(dw,WM_MOUSEMOVE,0,inside);
                        PostMessageW(dw,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(static_cast<short>(-120*std::atoi(docsWheel.c_str())))),MAKELPARAM(sp.x,sp.y));
                    }
                    if(settle==4 && !docsClick.empty()) {
                        int cx=std::atoi(docsClick.c_str()),cy=std::atoi(docsClick.c_str()+docsClick.find(',')+1);
                        PostMessageW(dw,WM_MOUSEMOVE,0,MAKELPARAM(cx,cy)); PostMessageW(dw,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(cx,cy)); PostMessageW(dw,WM_LBUTTONUP,0,MAKELPARAM(cx,cy));
                    }
                }
                if(stage==5 && settle==2 && !typed.empty()) {std::string t=typed; for(auto& ch:t) {if(ch=='~') ch='\n'; else if(ch=='_') ch=' ';} app.typeInEditor(t);}
                if(captureNow) {
                    if(!capture.empty() && !lab::saveBackBuffer(device,context,swapChain,capture+(stage==3?"-display.bmp":stage==4?"-board.bmp":"-code.bmp"))) {report("FAIL: capture\n"); exitCode=1; running=false;}
                    if(stage==3) {stage=4; app.showTab(1); if(!boardZoom.empty()) app.setBoardZoom(std::atof(boardZoom.c_str())); settle=0;}
                    else if(stage==4) {stage=5; app.showTab(2); settle=0;}
                    else {
                        // The reference saves its own picture when it next draws.
                        if(!capture.empty() && docs.isOpen()) docs.capture(capture+"-docs.bmp");
                        report("PASS: %s built, %d ticks with keys through the session\n",s.name.c_str(),tick); stage=6;
                    }
                }
            }
            swapChain->Present(1,0);
            docs.render();
            if(stage==6) running=false;
        }
    }
    ImGui::SetCurrentContext(mainContext);
    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext(mainContext);
    releaseTarget(); swapChain->Release(); context->Release(); device->Release();
    DestroyWindow(hwnd); UnregisterClassW(wc.lpszClassName,instance);
    return exitCode;
}
