// sweepiler in the browser: the pixel UI and the machine core in WebAssembly,
// drawn with WebGL 2 through SDL2. The session builds and ticks on pthreads
// (Web Workers), so the page needs cross-origin isolation (coi-serviceworker.js).
// The reference opens over the lab in the same canvas.
#include "PixelApp.h"
#include "PixelDocs.h"
#include "MachineFrame.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include <SDL.h>
#include <SDL_opengles2.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <zlib.h>
namespace {
SDL_Window* window{};
std::unique_ptr<ims::MachineSession> session;
std::unique_ptr<lab::PixelApp> app;
std::unique_ptr<lab::PixelDocs> docs;
std::string opened;   // a file the browser's picker wrote to the file system
std::string status;   // for web_status()
long long frames{};
bool wasReady{};
const std::filesystem::path cacheDir="/cache/frames";
// Frame caches live in IndexedDB (/cache), so a layout made once stays for the next visit.
void mountCache() {
    EM_ASM({
        FS.mkdir('/cache'); FS.mount(IDBFS,{},'/cache');
        FS.syncfs(true,function(err){try{FS.mkdir('/cache/frames');}catch(e){}});
    });
}
void saveCache() {EM_ASM({FS.syncfs(false,function(err){});});}
// A prebuilt frame missing from the cache, fetched gzipped from frames/ next
// to the page (frame_tool makes them). Runs on the session's builder (a
// worker), where a synchronous request is allowed; the bytes land in a
// malloc'd buffer that zlib inflates into the cache file.
EM_JS(uint8_t*,fetchSync,(const char* url,int* size),{
    try {
        var x=new XMLHttpRequest(); x.open('GET',UTF8ToString(url),false); x.responseType='arraybuffer'; x.send();
        if(x.status!==200 || !x.response) return 0;
        var bytes=new Uint8Array(x.response),p=_malloc(bytes.length); HEAPU8.set(bytes,p); HEAP32[size>>2]=bytes.length; return p;
    } catch(e) {return 0;}
});
bool fetchPrebuilt(const std::filesystem::path& path) {
    int size=0; uint8_t* data=fetchSync(("frames/"+path.filename().string()+".gz").c_str(),&size);
    if(!data) return false;
    z_stream z{}; bool ok=inflateInit2(&z,16+MAX_WBITS)==Z_OK;   // 16: a gzip header
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::binary);
    z.next_in=data; z.avail_in=static_cast<uInt>(size);
    std::vector<char> chunk(1<<20); int r=Z_OK;
    while(ok && r!=Z_STREAM_END) {
        z.next_out=reinterpret_cast<Bytef*>(chunk.data()); z.avail_out=static_cast<uInt>(chunk.size());
        r=inflate(&z,Z_NO_FLUSH); ok=r==Z_OK || r==Z_STREAM_END;
        out.write(chunk.data(),static_cast<std::streamsize>(chunk.size()-z.avail_out));
    }
    inflateEnd(&z); std::free(data); out.close();
    if(!ok) std::filesystem::remove(path);
    return ok;
}
// Phones. Where a whole-number scale would leave the lab's 400 x 250 screen at
// 1x, it takes a fractional scale instead. Upright, the lab stacks the screen,
// the status and the program list in one column (264 x 441) as wide as the
// phone. The status panel's key caps play the program.
struct PhoneLayout {bool portrait{}; float scale{}; ImVec2 origin{};};
PhoneLayout phoneLayout(float w,float h) {
    PhoneLayout l; const float fit=std::min(w/400,h/250);
    if(fit>=2) return l;
    const ImVec2 v=h>w?ImVec2{264,441}:ImVec2{400,250};
    l.portrait=h>w; l.scale=std::min(w/v.x,h/v.y);
    l.origin={std::floor((w-v.x*l.scale)/2),std::floor((h-v.y*l.scale)/2)};
    return l;
}
// The page's file inputs (shell.html) lie over the OPEN buttons, so a tap opens
// the browser's picker directly, as phones only allow from the tap itself.
EM_JS(void,placeFileInputs,(const float* r,int n),{if(Module.placeFileInputs) Module.placeFileInputs(Array.from(HEAPF32.subarray(r>>2,(r>>2)+n*4)));});
std::vector<float> fileInputs;
void syncFileInputs() {
    std::vector<float> r;
    if(!docs->isOpen()) for(const auto& a:app->areas().open) r.insert(r.end(),{a.x,a.y,a.z,a.w});
    if(r!=fileInputs) {fileInputs=r; placeFileInputs(r.data(),static_cast<int>(r.size()/4));}
}
void frame() {
    SDL_Event e; while(SDL_PollEvent(&e)) ImGui_ImplSDL2_ProcessEvent(&e);
    // The canvas fills the browser window.
    double cssW=0,cssH=0; emscripten_get_element_css_size("#canvas",&cssW,&cssH);
    int w=0,h=0; SDL_GetWindowSize(window,&w,&h);
    if(static_cast<int>(cssW)!=w || static_cast<int>(cssH)!=h) SDL_SetWindowSize(window,static_cast<int>(cssW),static_cast<int>(cssH));
    if(!opened.empty()) {app->openCodeFile(opened); opened.clear();}
    ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
    const auto& io=ImGui::GetIO();
    auto s=session->status();
    const PhoneLayout phone=phoneLayout(io.DisplaySize.x,io.DisplaySize.y);
    app->setPortrait(phone.portrait);
    app->setFixedLayout(phone.scale,phone.origin);   // scale 0: the lab's own whole-number fit
    if(docs->isOpen()) docs->render(); else app->frame(io.DisplaySize.x,io.DisplaySize.y);
    syncFileInputs();
    ImGui::Render();
    glViewport(0,0,static_cast<int>(io.DisplaySize.x*io.DisplayFramebufferScale.x),static_cast<int>(io.DisplaySize.y*io.DisplayFramebufferScale.y));
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if(s.ready && !wasReady) saveCache();
    wasReady=s.ready;
    // The session's progress and errors also go to the browser console.
    static std::string shown;
    std::string now=s.name+": "+(s.building?"building ":s.ready?"ready ":"")+s.progress+(s.error.empty()?"":" error: "+s.error);
    if(now!=shown) {std::printf("%s\n",now.c_str()); shown=now;}
    status=now+" | frames "+std::to_string(++frames)+" | mouse "+std::to_string(static_cast<int>(io.MousePos.x))+","+std::to_string(static_cast<int>(io.MousePos.y))+(io.MouseDown[0]?" down":" up");
}
}
// Called by the page when the file picker (OPEN) has written a file.
extern "C" EMSCRIPTEN_KEEPALIVE void web_opened(const char* path) {opened=path;}
// Touch (shell.html asks where a finger went down): 1 on the board, where a
// drag pans and two fingers zoom; 2 in the code editor, where a tap also brings
// up the phone's keyboard; 0 elsewhere, where a swipe scrolls.
extern "C" EMSCRIPTEN_KEEPALIVE int web_touch_mode(float x,float y) {
    if(!app || docs->isOpen()) return 0;
    auto in=[&](ImVec4 r){return r.z>r.x && x>=r.x && x<r.z && y>=r.y && y<r.w;};
    const auto& a=app->areas();
    return app->tab()==1 && in(a.board)?1:app->tab()==2 && in(a.editor)?2:0;
}
// Text and keys from the phone's keyboard (a hidden textarea in shell.html).
extern "C" EMSCRIPTEN_KEEPALIVE void web_type(const char* text) {ImGui::GetIO().AddInputCharactersUTF8(text);}
extern "C" EMSCRIPTEN_KEEPALIVE void web_key(int key) {
    const ImGuiKey k=key==1?ImGuiKey_Backspace:ImGuiKey_Enter;
    ImGui::GetIO().AddKeyEvent(k,true); ImGui::GetIO().AddKeyEvent(k,false);
}
// The session's state as text, for checking the page from the console.
extern "C" EMSCRIPTEN_KEEPALIVE const char* web_status() {return status.c_str();}
int main() {
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT,"#canvas");
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)!=0) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,0); SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
    window=SDL_CreateWindow("sweepiler",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,800,SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_GLContext gl=SDL_GL_CreateContext(window); if(!gl) return 1;
    SDL_GL_MakeCurrent(window,gl);
    IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGui::GetIO().IniFilename=nullptr;
    lab::loadFonts();
    ImGui_ImplSDL2_InitForOpenGL(window,gl); ImGui_ImplOpenGL3_Init("#version 300 es");
    mountCache();
    ims::fetchMissingFrame=fetchPrebuilt;
    session=std::make_unique<ims::MachineSession>(cacheDir);
    app=std::make_unique<lab::PixelApp>(*session);
    docs=std::make_unique<lab::PixelDocs>(nullptr,nullptr);
    app->setDocs(docs.get());
    docs->openSample=[](const std::string& name){app->openSample(name);};
    app->activate();
    EM_ASM({var s=document.getElementById('status'); if(s) s.remove();});
    emscripten_set_main_loop(frame,0,true);
    return 0;
}
