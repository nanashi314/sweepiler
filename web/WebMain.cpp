// sweepiler in the browser: the pixel UI and the machine core in WebAssembly,
// drawn with WebGL 2 through SDL2. The session builds and ticks on pthreads
// (Web Workers), so the page needs cross-origin isolation (coi-serviceworker.js).
// The reference opens over the lab in the same canvas.
#include "PixelApp.h"
#include "PixelDocs.h"
#include "MachineFrame.h"
#include "PixelFont.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include <SDL.h>
#include <SDL_opengles2.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <algorithm>
#include <cctype>
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
// 1x, it takes a fractional scale instead, and the program's keys get big
// buttons in the room left over. Upright, the lab stacks the screen, the
// status and the program list in one column (264 x 441) as wide as the phone,
// with the buttons below; turned, the buttons sit at its sides. A button sends
// its key's first binding as a key event, so the lab reads it like the keyboard.
struct PhoneLayout {bool portrait{}; float scale{}; ImVec2 origin{}; ImVec4 pads[2]{}; int padCount{};};   // pads: x, y, width, height
PhoneLayout phoneLayout(float w,float h) {
    PhoneLayout l; const float fit=std::min(w/400,h/250);
    if(fit>=2) return l;
    if(h>w) {
        // At least a fifth of the height stays for the buttons.
        l.portrait=true; l.scale=std::min(w/264,h*0.8f/441);
        const float screenW=264*l.scale,screenH=441*l.scale;
        l.origin={std::floor((w-screenW)/2),0};
        l.pads[0]={0,screenH,w,h-screenH}; l.padCount=1;
    } else {
        l.scale=fit;
        const float side=std::floor((w-400*fit)/2);
        l.origin={side,std::floor((h-250*fit)/2)};
        if(side>=70) {l.pads[0]={0,0,side,h}; l.pads[1]={w-side,0,side,h}; l.padCount=2;}
    }
    return l;
}
std::vector<uint8_t> padHeld;   // per key, held on a pad button
void keyPads(const PhoneLayout& l,const ims::MachineSession::Status& s,bool shown) {
    auto& io=ImGui::GetIO();
    const auto binds=lab::bindingsFor(s.keys);
    std::vector<uint8_t> held(s.keys.size(),0);
    if(shown && l.padCount && s.ready) {
        // Arrows in a cross (LEFT, RIGHT, UP or ROTATE, DOWN), the other keys in a column.
        int cross[4]={-1,-1,-1,-1}; std::vector<size_t> others;
        for(size_t i=0;i<s.keys.size();++i) {
            std::string k=s.keys[i]; for(auto& c:k) c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            int slot=k=="LEFT"?0:k=="RIGHT"?1:(k=="UP" || k=="ROTATE")?2:k=="DOWN"?3:-1;
            if(slot>=0 && cross[slot]<0) cross[slot]=static_cast<int>(i); else others.push_back(i);
        }
        const bool hasCross=cross[0]>=0 || cross[1]>=0 || cross[2]>=0 || cross[3]>=0;
        const ImVec4 a=l.pads[0],b=l.padCount>1?l.pads[1]:l.pads[0];
        // Upright, one pad holds both: the cross on the left, the column on the right.
        ImVec4 crossBox=l.padCount>1?a:ImVec4{a.x,a.y,a.z*0.55f,a.w},columnBox=l.padCount>1?b:ImVec4{a.x+a.z*0.55f,a.y,a.z*0.45f,a.w};
        if(!hasCross && l.padCount==1) columnBox=a;
        if(others.empty() && l.padCount==1) crossBox=a;
        const float size=std::floor(std::min(crossBox.z/3.3f,crossBox.w/3.3f));
        int window=0;
        auto button=[&](size_t key,ImVec2 c,float e,std::string label){
            ImGui::SetCursorScreenPos({c.x-e/2,c.y-e/2});
            ImGui::InvisibleButton(("##pad"+std::to_string(key)).c_str(),{e,e});
            const bool down=ImGui::IsItemActive(); held[key]=down;
            auto* d=ImGui::GetWindowDrawList(); const float u=std::max(1.0f,std::floor(e/24)),x=c.x-e/2,y=c.y-e/2,o=down?u:0;
            if(!down) d->AddRectFilled({x+u,y+u},{x+e+u,y+e+u},IM_COL32(0,0,0,255));
            d->AddRectFilled({x+o,y+o},{x+o+e,y+o+e},IM_COL32(0,0,0,255));
            d->AddRectFilled({x+o+u,y+o+u},{x+o+e-u,y+o+e-u},down?IM_COL32(255,236,39,255):IM_COL32(194,195,199,255));
            d->AddRectFilled({x+o+u,y+o+u},{x+o+e-u,y+o+2*u},IM_COL32(255,255,255,255));
            float fs=std::max(1.0f,std::floor(e*0.5f/7));
            while(fs>1 && static_cast<float>(lab::pixelfont::width(label))*fs>e*0.8f) --fs;
            lab::pixelfont::draw(d,{x+o+std::floor((e-static_cast<float>(lab::pixelfont::width(label))*fs)/2),y+o+std::floor((e-7*fs)/2)},fs,IM_COL32(0,0,0,255),label);
        };
        auto begin=[&](ImVec4 r){
            ImGui::SetNextWindowPos({r.x,r.y}); ImGui::SetNextWindowSize({r.z,r.w});
            ImGui::Begin(("##pads"+std::to_string(window++)).c_str(),nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoNav);
        };
        auto upper=[](std::string t){for(auto& c:t) c=static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return t.substr(0,5);};
        auto glyphFor=[&](int k){return upper(s.keys[static_cast<size_t>(cross[k])])=="ROTAT"?std::string("ROT"):std::string(k==0?"":k==1?"":k==2?"":"");};
        if(l.padCount>1) {
            // Turned: like a game pad, LEFT and RIGHT under the left thumb, the rest under the right.
            const float e=std::floor(std::min(a.z/2.3f,a.w/3.3f));
            std::vector<size_t> right; for(int k:{2,3}) if(cross[k]>=0) right.push_back(static_cast<size_t>(cross[k]));
            right.insert(right.end(),others.begin(),others.end());
            if(cross[0]>=0 || cross[1]>=0) {
                begin(a);
                for(int k:{0,1}) if(cross[k]>=0) button(static_cast<size_t>(cross[k]),{a.x+a.z/2+(k?0.55f:-0.55f)*e,a.y+a.w/2},e,glyphFor(k));
                ImGui::End();
            }
            if(!right.empty()) {
                begin(b);
                const float f=std::floor(std::min(e,b.w/(static_cast<float>(right.size())*1.15f)));
                float y=b.y+b.w/2-(static_cast<float>(right.size())-1)*f*0.575f;
                for(size_t k:right) {
                    int slot=-1; for(int c=0;c<4;++c) if(cross[c]==static_cast<int>(k)) slot=c;
                    button(k,{b.x+b.z/2,y},f,slot>=0?glyphFor(slot):upper(s.keys[k])); y+=f*1.15f;
                }
                ImGui::End();
            }
        } else {
        if(hasCross) {
            begin(crossBox);
            const ImVec2 c{crossBox.x+crossBox.z/2,crossBox.y+crossBox.w/2};
            const ImVec2 at[4]={{c.x-size,c.y},{c.x+size,c.y},{c.x,c.y-size},{c.x,c.y+size}};
            for(int k=0;k<4;++k) if(cross[k]>=0) button(static_cast<size_t>(cross[k]),at[k],size-4,glyphFor(k));
            ImGui::End();
        }
        if(!others.empty()) {
            begin(columnBox);
            const float e=std::floor(std::min({size-4,columnBox.z*0.7f,columnBox.w/(static_cast<float>(others.size())*1.2f)}));
            float y=columnBox.y+columnBox.w/2-(static_cast<float>(others.size())-1)*e*0.6f;
            for(size_t k:others) {button(k,{columnBox.x+columnBox.z/2,y},e,upper(s.keys[k])); y+=e*1.2f;}
            ImGui::End();
        }
        }
    }
    // Key events only on changes; a hidden pad lets go of its keys.
    padHeld.resize(s.keys.size(),0);
    for(size_t i=0;i<s.keys.size() && i<binds.size();++i) if(held[i]!=padHeld[i] && !binds[i].empty()) {io.AddKeyEvent(binds[i][0],held[i]!=0); padHeld[i]=held[i];}
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
    keyPads(phone,s,!docs->isOpen() && app->tab()!=2);
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
