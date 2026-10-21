#include "PixelDocs.h"
#include "LabCommon.h"
#include "PixelEditor.h"
#include "PixelFont.h"
#include "imgui.h"
#include <algorithm>
#include <iterator>
#include <cctype>
#include <cmath>
#include <sstream>
namespace lab {
namespace {
constexpr ImU32 rgb(unsigned v,unsigned a=255) {return IM_COL32((v>>16)&255,(v>>8)&255,v&255,a);}
constexpr ImU32 black=rgb(0x000000),navy=rgb(0x1D2B53),ash=rgb(0x5F574F),silver=rgb(0xC2C3C7),white=rgb(0xFFF1E8),
    yellow=rgb(0xFFEC27),sky=rgb(0x29ADFF),green=rgb(0x00E436),lavender=rgb(0x83769C),orange=rgb(0xFFA300),
    backdrop=rgb(0x10162B),panelFill=rgb(0x0B1020),codeFill=rgb(0x05070F);
bool ascii(std::string_view s) {for(unsigned char c:s) if(c>=0x80) return false; return true;}
// Lines of text that fit `width`, wrapped for Japanese: a line may break
// between any two Japanese characters and at spaces, never inside a run of
// ASCII letters and digits (a word, a name, "0..7"); closing punctuation does
// not start a line and opening brackets do not end one.
std::vector<std::string> wrapText(ImFont* font,float size,const std::string& s,float width) {
    struct Unit {std::string text; float w; bool space,ascii;};
    auto len=[](unsigned char b){return b<0x80?1:b>=0xF0?4:b>=0xE0?3:b>=0xC0?2:1;};
    auto measure=[&](const std::string& t){return font->CalcTextSizeA(size,1e9f,0,t.c_str()).x;};
    std::vector<Unit> units;
    for(size_t i=0;i<s.size();) {
        unsigned char c=static_cast<unsigned char>(s[i]);
        if(c==' ') {units.push_back({" ",measure(" "),true,true}); ++i; continue;}
        if(c<0x80) {size_t e=i; while(e<s.size() && static_cast<unsigned char>(s[e])<0x80 && s[e]!=' ') ++e; std::string t=s.substr(i,e-i); units.push_back({t,measure(t),false,true}); i=e; continue;}
        std::string t=s.substr(i,static_cast<size_t>(len(c))); units.push_back({t,measure(t),false,false}); i+=t.size();
    }
    static const char* noStart[]={"\xE3\x80\x81","\xE3\x80\x82","\xEF\xBC\x8C","\xEF\xBC\x8E","\xEF\xBC\x89","\xE3\x80\x8D","\xE3\x80\x8F","\xEF\xBC\x81","\xEF\xBC\x9F","\xEF\xBC\x9A","\xEF\xBC\x9B","\xE3\x83\xBC","\xE3\x83\xBB","\xE3\x81\x81","\xE3\x81\x83","\xE3\x81\x85","\xE3\x81\x87","\xE3\x81\x89","\xE3\x81\xA3","\xE3\x82\x83","\xE3\x82\x85","\xE3\x82\x87","\xE3\x82\xA1","\xE3\x82\xA3","\xE3\x82\xA5","\xE3\x82\xA7","\xE3\x82\xA9","\xE3\x83\x83","\xE3\x83\xA3","\xE3\x83\xA5","\xE3\x83\xA7"};
    static const char* noEnd[]={"\xEF\xBC\x88","\xE3\x80\x8C","\xE3\x80\x8E"};
    auto in=[](const std::string& t,const char* const* list,size_t n){for(size_t k=0;k<n;++k) if(t==list[k]) return true; return false;};
    std::vector<std::string> lines; std::vector<Unit> line; float w=0;
    auto flush=[&]{
        while(!line.empty() && line.back().space) line.pop_back();
        std::string t; for(const auto& u:line) t+=u.text; lines.push_back(t); line.clear(); w=0;
    };
    for(size_t i=0;i<units.size();++i) {
        const Unit& u=units[i];
        if(line.empty() && u.space) continue;                     // no space at the start of a line
        if(w+u.w>width && !line.empty() && !u.space && !in(u.text,noStart,std::size(noStart))) {
            // Carry an opening bracket over to the new line with what follows it.
            std::vector<Unit> carry;
            while(!line.empty() && in(line.back().text,noEnd,std::size(noEnd))) {carry.insert(carry.begin(),line.back()); line.pop_back();}
            if(!line.empty()) {flush(); for(auto& c:carry) {line.push_back(c); w+=c.w;}}
            else for(auto& c:carry) {line.push_back(c); w+=c.w;}
        }
        if(u.ascii && !u.space && u.w>width) {
            // A word longer than the line is split where it must be.
            std::string part; float pw=0;
            for(char ch:u.text) {float cw=measure(std::string(1,ch)); if(w+pw+cw>width && (!line.empty() || !part.empty())) {line.push_back({part,pw,false,true}); flush(); part.clear(); pw=0;} part+=ch; pw+=cw;}
            line.push_back({part,pw,false,true}); w+=pw; continue;
        }
        line.push_back(u); w+=u.w;
    }
    if(!line.empty()) flush();
    return lines;
}
// The groups in order of first appearance.
std::vector<std::string> groupsOf(const std::vector<DocTopic>& t) {
    std::vector<std::string> g; for(const auto& x:t) if(std::find(g.begin(),g.end(),x.group)==g.end()) g.push_back(x.group); return g;
}
}
PixelDocs::PixelDocs(ID3D11Device* device,ID3D11DeviceContext* context):device_(device),context_(context) {
    open_.assign(groupsOf(mineCTopics()).size(),false); if(!open_.empty()) open_[0]=true;
}
void PixelDocs::openGroup(const std::string& group,bool open) {
    auto groups=groupsOf(mineCTopics());
    for(size_t g=0;g<groups.size();++g) if(groups[g]==group) open_[g]=open;
}
float PixelDocs::treeWidth(float width,float v) {
    // A third of the window, at most 150 virtual pixels, and never narrower than its title.
    return std::max(std::min(150*v,width*0.32f),static_cast<float>(12+pixelfont::width("REFERENCE"))*v);
}
void PixelDocs::ui(float width,float height) {
    const auto& topics=mineCTopics(); const auto groups=groupsOf(topics);
    ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({width,height});
    ImGui::Begin("##docs",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoScrollWithMouse);
    auto* d=ImGui::GetWindowDrawList();
    const float v=pixel_>0?pixel_:width>=1500 && height>=900?3.0f:2.0f;   // one virtual pixel, in screen pixels
    ImFont* jp=jpFont();   // the font of whichever context draws (the window's own, or the lab's when recording)
    auto R=[&](float x,float y,float w,float h,ImU32 c){d->AddRectFilled({x,y},{x+w,y+h},c);};
    auto panel=[&](float x,float y,float w,float h){R(x,y,w,h,black); R(x+v,y+v,w-2*v,h-2*v,silver); R(x+2*v,y+2*v,w-4*v,h-4*v,ash); R(x+3*v,y+3*v,w-6*v,h-6*v,panelFill);};
    auto label=[&](float x,float y,std::string_view s,ImU32 c,float size){
        if(ascii(s)) {std::string u(s); for(auto& ch:u) ch=static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); pixelfont::draw(d,{x,y+v},v*size/9,c,u);}
        else d->AddText(jp,size*v,{x,y},c,s.data(),s.data()+s.size());
    };
    auto button=[&](const char* id,float x,float y,float w,float h,const char* text,ImU32 face){
        ImGui::SetCursorScreenPos({x,y}); bool hit=ImGui::InvisibleButton(id,{w,h}); bool hov=ImGui::IsItemHovered(),down=ImGui::IsItemActive();
        float o=down?v:0; if(!down) R(x+v,y+v,w,h,black);
        R(x+o,y+o,w,h,black); R(x+o+v,y+o+v,w-2*v,h-2*v,hov?white:face); R(x+o+v,y+o+v,w-2*v,v,rgb(0xFFFFFF,90));
        float tw=static_cast<float>(pixelfont::width(text))*v; pixelfont::draw(d,{x+o+(w-tw)/2,y+o+(h-7*v)/2},v,black,text);
        return hit;
    };
    R(0,0,width,height,backdrop);
    // ---- the topic tree
    const float pad=6*v,treeW=treeWidth(width,v);
    panel(pad,pad,treeW,height-2*pad);
    pixelfont::draw(d,{pad+9*v,pad+5*v},v,yellow,"REFERENCE");
    const float row=11*v,treeTop=pad+16*v,treeBottom=height-pad-4*v;
    if(reveal_) {for(size_t g=0;g<groups.size();++g) if(groups[g]==topics[topic_].group) open_[g]=true; reveal_=false;}
    const ImVec2 mouse=ImGui::GetIO().MousePos;
    auto inside=[&](float x0,float y0,float x1,float y1){return mouse.x>=x0 && mouse.x<x1 && mouse.y>=y0 && mouse.y<y1;};
    if(inside(pad,treeTop,pad+treeW,treeBottom)) treeScroll_-=ImGui::GetIO().MouseWheel*3*row;
    float ty=treeTop-treeScroll_;
    d->PushClipRect({pad,treeTop},{pad+treeW,treeBottom},true);
    for(size_t g=0;g<groups.size();++g) {
        ImGui::SetCursorScreenPos({pad+4*v,ty});
        bool visible=ty+row>treeTop && ty<treeBottom;
        if(ImGui::InvisibleButton(("##g"+std::to_string(g)).c_str(),{treeW-8*v,row}) && visible) open_[g]=!open_[g];
        bool hov=visible && ImGui::IsItemHovered();
        if(hov) R(pad+4*v,ty,treeW-8*v,row,rgb(0x1D2B53,160));
        pixelfont::draw(d,{pad+6*v,ty+2*v},v,orange,open_[g]?"\x03":"\x05");
        label(pad+14*v,ty+1*v,groups[g],hov?white:orange,8.5f);
        ty+=row;
        if(!open_[g]) continue;
        for(size_t t=0;t<topics.size();++t) {
            if(groups[g]!=topics[t].group) continue;
            ImGui::SetCursorScreenPos({pad+4*v,ty});
            bool shown=ty+row>treeTop && ty<treeBottom;
            if(ImGui::InvisibleButton(("##t"+std::to_string(t)).c_str(),{treeW-8*v,row}) && shown) topic_=t;
            bool on=t==topic_,h2=shown && ImGui::IsItemHovered();
            if(on) R(pad+4*v,ty,treeW-8*v,row,navy); else if(h2) R(pad+4*v,ty,treeW-8*v,row,rgb(0x1D2B53,120));
            if(on) pixelfont::draw(d,{pad+14*v,ty+2*v},v,yellow,"\x01");
            label(pad+22*v,ty+1*v,topics[t].title,on?yellow:(h2?white:silver),8.0f);
            ty+=row;
        }
    }
    d->PopClipRect();
    const float treeContent=ty+treeScroll_-treeTop;
    treeScroll_=std::clamp(treeScroll_,0.0f,std::max(0.0f,treeContent-(treeBottom-treeTop)));
    // ---- the page
    const float cx=pad*2+treeW,cw=width-cx-pad,ch=height-2*pad;
    panel(cx,pad,cw,ch);
    const DocTopic& page=topics[topic_];
    if(scrolledFor_!=topic_) {scroll_=0; scrolledFor_=topic_;}
    // Previous / next.
    if(topic_>0 && button("##prev",cx+cw-98*v,pad+5*v,44*v,11*v,"\x04 PREV",silver)) {topic_--; reveal_=true;}
    if(topic_+1<topics.size() && button("##next",cx+cw-51*v,pad+5*v,44*v,11*v,"NEXT \x05",silver)) {topic_++; reveal_=true;}
    // Open the topic's group in the tree when paging.
    const float left=cx+10*v,right=cx+cw-10*v,top=pad+20*v,bottom=pad+ch-6*v,textW=right-left;
    auto& io=ImGui::GetIO();
    if(inside(cx,top,cx+cw,bottom)) scroll_-=io.MouseWheel*40*v;
    if(ImGui::IsKeyPressed(ImGuiKey_PageDown)) scroll_+=bottom-top-20*v;
    if(ImGui::IsKeyPressed(ImGuiKey_PageUp)) scroll_-=bottom-top-20*v;
    if(ImGui::IsKeyPressed(ImGuiKey_DownArrow)) scroll_+=20*v;
    if(ImGui::IsKeyPressed(ImGuiKey_UpArrow)) scroll_-=20*v;
    if(ImGui::IsKeyPressed(ImGuiKey_Home)) scroll_=0;
    if(ImGui::IsKeyPressed(ImGuiKey_End)) scroll_=1e9f;
    d->PushClipRect({cx+3*v,top-2*v},{cx+cw-3*v,bottom},true);
    float y=top-scroll_;
    label(left,pad+4*v,page.group,lavender,8.0f);
    label(left,y,page.title,yellow,13.0f); y+=15*v; R(left,y,textW,v,sky); y+=6*v;
    const float body=8.5f*v;
    auto para=[&](float x,const std::string& s,ImU32 c){
        const float lh=body*1.3f;   // line pitch with room between lines
        for(const auto& l:wrapText(jp,body,s,right-x)) {d->AddText(jp,body,{x,y},c,l.c_str()); y+=lh;}
        y+=3*v;
    };
    std::istringstream in(page.body); std::string line,paragraph; bool code=false; std::vector<std::string> block;
    auto flush=[&]{if(!paragraph.empty()) {para(left,paragraph,white); paragraph.clear(); y+=2*v;}};
    const CodeColors& cc=pico8Code();
    while(std::getline(in,line)) {
        if(line.rfind("```",0)==0) {
            flush();
            if(code) {
                // A code block in the pixel font with the editor's colours.
                float bh=static_cast<float>(block.size())*10*v+8*v; R(left,y,textW,bh,ash); R(left+v,y+v,textW-2*v,bh-2*v,codeFill);
                bool inBlock=false; float ly=y+4*v;
                for(const auto& l:block) {drawCodeLine(d,{left+6*v,ly},v,l,inBlock,cc); ly+=10*v;}
                y+=bh+6*v; block.clear();
            }
            code=!code; continue;
        }
        if(code) {block.push_back(line); continue;}
        if(line.empty()) {flush(); continue;}
        if(line.rfind("- ",0)==0) {flush(); R(left+2*v,y+4*v,3*v,3*v,green); para(left+9*v,line.substr(2),white); continue;}
        if(line.rfind("| ",0)==0) {
            flush();
            std::string rest=line.substr(2); size_t bar=rest.find(" | ");
            std::string a=bar==std::string::npos?rest:rest.substr(0,bar),b=bar==std::string::npos?"":rest.substr(bar+3);
            if(!b.empty() && b.back()=='|') b.pop_back();
            float colW=std::min(110*v,textW*0.38f);
            float y0=y;
            // A left cell wider than its column (a narrow page) puts the description on the next line,
            // indented; a code cell wider than the whole page breaks at its spaces.
            auto width=[&](const std::string& t){return ascii(t)?static_cast<float>(pixelfont::width(t))*v:jp->CalcTextSizeA(body,1e9f,0,t.c_str()).x;};
            const bool stacked=2*v+width(a)+4*v>colW;
            std::vector<std::string> cells{a};
            if(stacked && ascii(a) && 2*v+width(a)>textW) {
                cells.clear(); std::istringstream words(a); std::string word,run;
                while(words>>word) {std::string t=run.empty()?word:run+" "+word; if(!run.empty() && 2*v+width(t)>textW) {cells.push_back(run); run=word;} else run=t;}
                if(!run.empty()) cells.push_back(run);
            }
            float cy=y;
            for(const auto& c:cells) {if(ascii(c)) {bool ib=false; drawCodeLine(d,{left+2*v,cy+v},v,c,ib,cc);} else d->AddText(jp,body,{left+2*v,cy},sky,c.c_str()); cy+=11*v;}
            if(stacked) {y=cy; para(left+12*v,b,silver);}
            else para(left+colW,b,silver);
            y=std::max(y,y0+10*v)+v; R(left,y-v,textW,std::max(1.0f,v*0.5f),rgb(0x1D2B53));
            continue;
        }
        if(line.rfind("@sample ",0)==0) {
            flush(); std::string name=line.substr(8);
            if(button(("##open"+name).c_str(),left,y,94*v,13*v,"OPEN IN EDITOR",green) && openSample) openSample(name);
            y+=19*v; continue;
        }
        // Source lines of one paragraph join directly, with a space between two ASCII words.
        bool space=!paragraph.empty() && std::isalnum(static_cast<unsigned char>(paragraph.back())) && std::isalnum(static_cast<unsigned char>(line.front()));
        paragraph+=(space?" ":"")+line;
    }
    flush();
    const float content=y+scroll_-top;
    scroll_=std::clamp(scroll_,0.0f,std::max(0.0f,content-(bottom-top)+10*v));
    d->PopClipRect();
    // A scroll bar when the page is long; its thumb can be dragged.
    if(content>bottom-top) {
        const float range=std::max(1.0f,content-(bottom-top)+10*v),barH=std::max(10*v,(bottom-top)*(bottom-top)/content);
        ImGui::SetCursorScreenPos({cx+cw-9*v,top}); ImGui::InvisibleButton("##bar",{7*v,bottom-top});
        if(ImGui::IsItemActive()) scroll_=std::clamp((io.MousePos.y-top-barH/2)/(bottom-top-barH)*range,0.0f,range);
        float barY=top+(bottom-top-barH)*scroll_/range;
        R(cx+cw-7*v,top,3*v,bottom-top,navy); R(cx+cw-7*v,barY,3*v,barH,ImGui::IsItemHovered() || ImGui::IsItemActive()?white:silver);
    }
    ImGui::End();
}
}
