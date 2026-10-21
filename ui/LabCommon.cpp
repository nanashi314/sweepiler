#include "LabCommon.h"
#include "PixelFont.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
namespace lab {
std::vector<std::vector<ImGuiKey>> bindingsFor(const std::vector<std::string>& keys) {
    std::vector<std::vector<ImGuiKey>> out; const ImGuiKey spare[]={ImGuiKey_Z,ImGuiKey_X,ImGuiKey_C,ImGuiKey_V}; size_t next=0;
    for(auto k:keys) {
        for(auto& c:k) c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if(k=="LEFT") out.push_back({ImGuiKey_LeftArrow,ImGuiKey_A});
        else if(k=="RIGHT") out.push_back({ImGuiKey_RightArrow,ImGuiKey_D});
        else if(k=="UP" || k=="ROTATE") out.push_back({ImGuiKey_UpArrow,ImGuiKey_W,ImGuiKey_Space});
        else if(k=="DOWN") out.push_back({ImGuiKey_DownArrow,ImGuiKey_S});
        else if(next<4) out.push_back({spare[next++]});
        else out.push_back({});
    }
    return out;
}
void feedKeys(ims::MachineSession& session,const ims::MachineSession::Status& s,const std::vector<uint8_t>& extra,bool keyboard) {
    if(!s.ready) return;
    keyboard=keyboard && !ImGui::GetIO().WantTextInput;
    auto binds=bindingsFor(s.keys);
    for(size_t i=0;i<binds.size();++i) {
        bool held=i<extra.size() && extra[i],down=false;
        if(keyboard) for(auto k:binds[i]) {held|=ImGui::IsKeyDown(k); down|=ImGui::IsKeyPressed(k,false);}
        if(down) session.pressKey(i);
        session.setKey(i,held);
    }
}
std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary); if(!in) throw std::runtime_error("Cannot open "+path.string());
    std::stringstream t; t<<in.rdbuf(); return t.str();
}
// Each ImGui context (the lab, the reference window) keeps its own in io.UserData.
ImFont* jpFont() {auto* f=static_cast<ImFont*>(ImGui::GetIO().UserData); return f?f:ImGui::GetFont();}
int errorLineOf(const std::string& message) {
    auto at=message.find("line "); if(at==std::string::npos) return 0;
    int n=0; for(size_t i=at+5;i<message.size() && std::isdigit(static_cast<unsigned char>(message[i]));++i) n=n*10+(message[i]-'0');
    return n;
}
void BoardCamera::show(const ims::FrameMachine& m,ImVec2 p0,ImVec2 size,bool hovered,bool active,const CellFn& cell,const std::function<void(float,float,float)>& border) {
    if(m.frames().empty()) return;
    const auto& f=m.frame(); int64_t newest=m.frames().back().index;
    double left=static_cast<double>(m.frameX(newest)+f.grid.origin.x),right=static_cast<double>(m.frameX(newest)+f.right);
    double top=static_cast<double>(f.grid.origin.y),bottom=top+f.grid.height;
    if(zoom<=0) zoom=std::min(size.x/(right-left),size.y/(bottom-top))*0.95;
    // Following keeps the view where it is relative to the newest frame: a new
    // frame slides it by one frame width, so dragging never turns it off.
    if(follow && newest!=followed) {
        if(followed<0 || followed>newest) {x=(left+right)/2; y=(top+bottom)/2;}
        else x+=static_cast<double>(m.frameX(newest)-m.frameX(followed));
        followed=newest;
    }
    // Pan by dragging, zoom with the wheel around the mouse.
    auto& io=ImGui::GetIO();
    if(active && ImGui::IsMouseDragging(ImGuiMouseButton_Left,0)) {x-=io.MouseDelta.x/zoom; y-=io.MouseDelta.y/zoom;}
    if(hovered && io.MouseWheel!=0) {
        double mx=x+(io.MousePos.x-p0.x-size.x/2)/zoom,my=y+(io.MousePos.y-p0.y-size.y/2)/zoom;
        zoom=std::clamp(zoom*std::pow(1.2,io.MouseWheel),0.02,48.0);
        x=mx-(io.MousePos.x-p0.x-size.x/2)/zoom; y=my-(io.MousePos.y-p0.y-size.y/2)/zoom;
    }
    auto sx=[&](double v){return static_cast<float>(p0.x+size.x/2+(v-x)*zoom);};
    auto sy=[&](double v){return static_cast<float>(p0.y+size.y/2+(v-y)*zoom);};
    auto all=m.bounds();
    int64_t x0=std::max(all.min.x,static_cast<int64_t>(std::floor(x-size.x/2/zoom))),x1=std::min(all.max.x,static_cast<int64_t>(std::ceil(x+size.x/2/zoom)));
    int64_t y0=std::max(all.min.y,static_cast<int64_t>(std::floor(y-size.y/2/zoom))),y1=std::min(all.max.y,static_cast<int64_t>(std::ceil(y+size.y/2/zoom)));
    // Far out, sample so a frame stays cheap to draw.
    int64_t step=std::max<int64_t>(1,static_cast<int64_t>(std::ceil(3/zoom)));
    float e=static_cast<float>(zoom*static_cast<double>(step));
    for(int64_t cy=y0-(y0%step+step)%step;cy<=y1;cy+=step) for(int64_t cx=x0-(x0%step+step)%step;cx<=x1;cx+=step) {
        using Cell=ims::FrameMachine::Cell;
        int digit=0; auto c=m.cell({cx,cy},&digit); if(c==Cell::Outside) continue;
        const bool wire=c==Cell::Covered || c==Cell::Mine;
        if(c==Cell::Covered) {
            // Opened: the count of mines around it (only worth counting when digits are drawn).
            c=Cell::Digit; digit=0;
            if(e>=8) for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx||dy) {
                Cell k=m.cell({cx+dx,cy+dy}); digit+=k==Cell::Mine || k==Cell::FixedMine || k==Cell::InputMine;
            }
        }
        cell(c,digit,sx(static_cast<double>(cx)),sy(static_cast<double>(cy)),e,wire);
    }
    for(const auto& fr:m.frames()) border(sx(static_cast<double>(m.frameX(fr.index)+f.right)+0.5),sy(top),sy(bottom));
}
void drawClassicCell(ImDrawList* d,ims::FrameMachine::Cell c,int digit,float x,float y,float e,bool wire) {
    using Cell=ims::FrameMachine::Cell;
    // The circuit's cells are a touch bluer than the scaffold's grey.
    const ImU32 face=wire?IM_COL32(184,192,208,255):IM_COL32(192,192,192,255),light=IM_COL32(255,255,255,255),dark=IM_COL32(128,128,128,255);
    const bool mine=c==Cell::Mine || c==Cell::FixedMine || c==Cell::InputMine;
    if(e<8) {
        // Far out: digits light, covered cells grey, found mines red, fixed mines dark, keys orange or green.
        ImU32 col=c==Cell::Digit?(wire?IM_COL32(196,206,224,255):IM_COL32(208,208,208,255)):c==Cell::Mine?IM_COL32(220,40,40,255):c==Cell::FixedMine?IM_COL32(52,52,58,255):
            c==Cell::InputMine?IM_COL32(255,163,0,255):c==Cell::InputSafe?IM_COL32(0,200,90,255):IM_COL32(150,150,150,255);
        d->AddRectFilled({x,y},{x+e,y+e},col); return;
    }
    const float b=std::max(1.0f,std::floor(e/8));     // bevel width
    if(c==Cell::Digit) {
        // Revealed: flat, with the grid line on the top and left.
        d->AddRectFilled({x,y},{x+e,y+e},face);
        d->AddRectFilled({x,y},{x+e,y+1},dark); d->AddRectFilled({x,y},{x+1,y+e},dark);
        static const ImU32 ink[9]={IM_COL32(150,150,150,255),IM_COL32(0,0,255,255),IM_COL32(0,128,0,255),IM_COL32(255,0,0,255),IM_COL32(0,0,128,255),
            IM_COL32(128,0,0,255),IM_COL32(0,128,128,255),IM_COL32(0,0,0,255),IM_COL32(128,128,128,255)};
        if(e>=9) {
            float fs=std::floor(e/9); char t[2]={static_cast<char>('0'+std::clamp(digit,0,8)),0};
            pixelfont::draw(d,{x+std::floor((e-5*fs)/2)+1,y+std::floor((e-7*fs)/2)+1},fs,ink[std::clamp(digit,0,8)],t);
        }
        return;
    }
    // Raised tile.
    ImU32 tile=c==Cell::InputMine?IM_COL32(255,190,90,255):c==Cell::InputSafe?IM_COL32(150,230,170,255):face;
    d->AddRectFilled({x,y},{x+e,y+e},tile);
    d->AddRectFilled({x,y},{x+e,y+b},light); d->AddRectFilled({x,y},{x+b,y+e},light);
    d->AddRectFilled({x,y+e-b},{x+e,y+e},dark); d->AddRectFilled({x+e-b,y},{x+e,y+e},dark);
    if(mine) {
        // Flag: red pennant on a black pole and base.
        auto P=[&](float u,float v){return ImVec2{x+e*u/16,y+e*v/16};};
        d->AddTriangleFilled(P(9,3),P(9,8.5f),P(4,5.75f),IM_COL32(255,0,0,255));
        d->AddRectFilled(P(8.5f,3),P(10,12),IM_COL32(0,0,0,255));
        d->AddRectFilled(P(6,10.5f),P(12,12),IM_COL32(0,0,0,255));
        d->AddRectFilled(P(4.5f,12),P(13.5f,13.5f),IM_COL32(0,0,0,255));
    }
}
}
