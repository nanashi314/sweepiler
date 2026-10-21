#include "PixelApp.h"
#include "PixelFont.h"
#include "PixelDocs.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
namespace lab {
namespace {
// PICO-8 palette.
constexpr ImU32 rgb(unsigned v,unsigned a=255) {return IM_COL32((v>>16)&255,(v>>8)&255,v&255,a);}
constexpr ImU32 black=rgb(0x000000),navy=rgb(0x1D2B53),plum=rgb(0x7E2553),forest=rgb(0x008751),brown=rgb(0xAB5236),
    ash=rgb(0x5F574F),silver=rgb(0xC2C3C7),white=rgb(0xFFF1E8),red=rgb(0xFF004D),orange=rgb(0xFFA300),yellow=rgb(0xFFEC27),
    green=rgb(0x00E436),sky=rgb(0x29ADFF),lavender=rgb(0x83769C),pink=rgb(0xFF77A8),peach=rgb(0xFFCCAA);
constexpr ImU32 backdrop=rgb(0x10162B),panelFill=rgb(0x0B1020);
// Tick intervals of the speed bar, slow to fast; 0 runs ticks back to back.
constexpr int speeds[]={1500,800,500,350,250,120,60,0};
std::string upper(std::string s) {for(auto& c:s) c=static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return s;}
// The glyph shown on a key cap.
std::string capFor(const std::vector<ImGuiKey>& keys) {
    if(keys.empty()) return "-";
    switch(keys[0]) {
    case ImGuiKey_LeftArrow: return "\x04"; case ImGuiKey_RightArrow: return "\x05";
    case ImGuiKey_UpArrow: return "\x02"; case ImGuiKey_DownArrow: return "\x03";
    default: return std::string(1,ImGui::GetKeyName(keys[0])[0]);
    }
}
}
constexpr const char* newProgram=R"MINEC(// A new MineC program. F5 runs it.
#display 8 8
input hold left;
input hold right;

uint3 x = 3;

void tick() {
    if (left) x--;
    if (right) x++;
}

void draw() {
    pixel(x, 7, 1);
}
)MINEC";
PixelApp::PixelApp(ims::MachineSession& session):session_(session),programs_(ims::machinePrograms()) {loadCode(0);}
void PixelApp::choose(size_t program) {
    choice_=program; file_.reset();
    if(!dirty_) loadCode(program);   // edited code stays until it is saved or replaced
}
void PixelApp::newCode(const std::string& text) {
    tab_=2; editor_.setText(text); codeLanguage_=ims::ProgramLanguage::MineC; codeName_="untitled"; codePath_.reset(); dirty_=true; error_.clear(); errorLine_=0; editor_.focus(); samples_=false;
}
void PixelApp::loadCode(size_t program) {
    const auto& p=programs_[program];
    codeLanguage_=p.language; codeName_=p.label; codePath_.reset(); dirty_=false; error_.clear(); errorLine_=0;
    editor_.setText(p.source);
}
void PixelApp::openSample(const std::string& label) {
    for(size_t i=0;i<programs_.size();++i) if(programs_[i].label==label) {choice_=i; file_.reset(); loadCode(i); tab_=2; samples_=false; editor_.focus();}
}
void PixelApp::openCodeFile(const std::filesystem::path& f) {
    tab_=2;
    try {editor_.setText(readTextFile(f)); codePath_=f; codeName_=f.stem().string(); codeLanguage_=f.extension()==".imsg"?ims::ProgramLanguage::Imsg:ims::ProgramLanguage::MineC; dirty_=false; error_.clear(); errorLine_=0; samples_=false;}
    catch(const std::exception& e) {error_=e.what();}
}
bool PixelApp::runCodeFile(const std::filesystem::path& path) {
    editor_.setText(readTextFile(path)); codePath_=path; codeName_=path.stem().string(); dirty_=false;
    codeLanguage_=path.extension()==".imsg"?ims::ProgramLanguage::Imsg:ims::ProgramLanguage::MineC;
    runCode(); if(!error_.empty()) tab_=2;
    return error_.empty();
}
void PixelApp::runCode() {
    try {
        session_.build(ims::compileProgramText(editor_.text(),codeLanguage_,codeName_));
        camera_.fit(); error_.clear(); errorLine_=0; tab_=0;
    } catch(const std::exception& e) {error_=e.what(); errorLine_=errorLineOf(error_);}
}
void PixelApp::activate() {
    ImGui::StyleColorsDark();
    auto& s=ImGui::GetStyle();
    s.WindowPadding={0,0}; s.WindowBorderSize=0; s.WindowRounding=0; s.ItemSpacing={0,0};
}
void PixelApp::buildChosen() {
    try {
        session_.build(file_?ims::compileProgramFile(*file_):programs_[choice_].compile());
        camera_.fit(); error_.clear(); errorLine_=0;
    } catch(const std::exception& e) {error_=e.what(); errorLine_=errorLineOf(error_);}
}
void PixelApp::rect(float x,float y,float w,float h,ImU32 c) {draw_->AddRectFilled(at(x,y),at(x+w,y+h),c);}
void PixelApp::text(float x,float y,std::string_view t,ImU32 c,float size) {pixelfont::draw(draw_,at(x,y),scale_*size,c,t);}
void PixelApp::textCentered(float cx,float y,std::string_view t,ImU32 c,float size) {text(cx-static_cast<float>(pixelfont::width(t))*size/2,y,t,c,size);}
void PixelApp::panel(float x,float y,float w,float h,std::string_view title) {
    // Chunky frame: black outline, light rim, dark inner shadow.
    rect(x,y,w,h,black); rect(x+1,y+1,w-2,h-2,silver); rect(x+2,y+2,w-4,h-4,ash); rect(x+3,y+3,w-6,h-6,panelFill);
    if(!title.empty()) {
        float tw=static_cast<float>(pixelfont::width(title));
        rect(x+6,y-1,tw+6,9,black); rect(x+7,y,tw+4,7,navy);
        text(x+9,y,title,yellow);
    }
}
bool PixelApp::button(const char* id,float x,float y,float w,float h,std::string_view label,ImU32 face,ImU32 ink,bool enabled) {
    ImGui::SetCursorScreenPos(at(x,y));
    bool clicked=ImGui::InvisibleButton(id,{w*scale_,h*scale_}) && enabled;
    bool hovered=enabled && ImGui::IsItemHovered(),down=enabled && ImGui::IsItemActive();
    if(!enabled) {face=ash; ink=lavender;}
    float o=down?1.0f:0.0f;
    if(!down) rect(x+1,y+1,w,h,black);                         // drop shadow
    rect(x+o,y+o,w,h,black);
    rect(x+o+1,y+o+1,w-2,h-2,face);
    rect(x+o+1,y+o+1,w-2,1,hovered?white:rgb(0xFFFFFF,90));    // bevel highlight
    rect(x+o+1,y+o+h-2,w-2,1,rgb(0x000000,90));                // bevel shadow
    textCentered(x+o+w/2,y+o+(h-7)/2,label,ink);
    return clicked;
}
void PixelApp::frame(float width,float height) {
    auto s=session_.status();
    // Typing code does not steer the program; key caps clicked on screen do.
    if(tab_==2) capHeld_.clear();
    feedKeys(session_,s,capHeld_,!(editor_.focused() && tab_==2));
    time_=ImGui::GetTime();
    // Whole-number scale of the virtual screen, centred.
    const ImVec2 v=virtualSize();
    scale_=std::max(1.0f,std::floor(std::min(width/v.x,height/v.y)));
    origin_={std::floor((width-v.x*scale_)/2),std::floor((height-v.y*scale_)/2)};
    if(fixedScale_>0) {scale_=fixedScale_; origin_=fixedOrigin_;}
    ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({width,height});
    ImGui::Begin("##pixel",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBringToFrontOnFocus|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoScrollWithMouse);
    draw_=ImGui::GetWindowDrawList();
    draw_->AddRectFilled({0,0},{width,height},backdrop);
    menu(s);
    // Tabs over the main view.
    // The code editor takes the status panel's room too.
    const float mx=portrait_?4.0f:140.0f,my=16,mw=256,mh=tab_==2?230.0f:172.0f;
    auto tab=[&](const char* id,float x,float w,const char* label,int index){
        ImGui::SetCursorScreenPos(at(x,4));
        if(ImGui::InvisibleButton(id,{w*scale_,13*scale_})) tab_=index;
        bool on=tab_==index;
        rect(x,4,w,13,black); rect(x+1,5,w-2,12,on?silver:ash); rect(x+2,6,w-4,on?11.0f:10.0f,on?panelFill:navy);
        textCentered(x+w/2,8,label,on?yellow:(ImGui::IsItemHovered()?white:silver));
    };
    tab("##tscreen",mx,58,"SCREEN",0);
    tab("##tboard",mx+60,52,"BOARD",1);
    tab("##tcode",mx+114,46,dirty_?"CODE*":"CODE",2);
    panel(mx,my,mw,mh,"");
    const float openX[]={3,63,117},openW[]={52,46,40};
    rect(mx+openX[tab_],my,openW[tab_],3,panelFill);   // open the panel under the chosen tab
    if(tab_==0) screen(s,mx+6,my+6,mw-12,mh-12); else if(tab_==1) board(mx+4,my+4,mw-8,mh-8); else code(mx+4,my+4,mw-8,mh-8);
    if(tab_!=2) status(s);
    // Faint scanlines over everything.
    for(float y=0;y<v.y;y+=2) rect(0,y+1,v.x,0.5f,rgb(0x000000,40));
    ImGui::End();
}
void PixelApp::menu(const ims::MachineSession::Status& s) {
    // Upright, the list goes under the status panel, as wide as the screen and five rows shorter.
    const float x=4,y=portrait_?250.0f:4.0f,w=portrait_?256.0f:132.0f,h=portrait_?187.0f:242.0f;
    panel(x,y,w,h,"PROGRAMS");
    // Program list with a blinking cursor.
    // The list shows 13 rows (8 upright); the wheel scrolls it, and the chosen program stays in view.
    const int rows=portrait_?8:13; const float listTop=y+12;
    const int count=static_cast<int>(programs_.size())+1;
    if(choice_!=listFollowed_) {
        int c=static_cast<int>(choice_);
        if(c<listScroll_) listScroll_=c; else if(c>=listScroll_+rows) listScroll_=c-rows+1;
        listFollowed_=choice_;
    }
    const ImVec2 m=ImGui::GetIO().MousePos;
    if(m.x>=at(x,listTop).x && m.x<at(x+w,listTop).x && m.y>=at(x,listTop).y && m.y<at(x,listTop+rows*11).y)
        listScroll_-=static_cast<int>(ImGui::GetIO().MouseWheel*2);
    listScroll_=std::clamp(listScroll_,0,std::max(0,count-rows));
    float ly=listTop;
    int row=0;
    bool blink=std::fmod(time_,0.8)<0.5;
    auto item=[&](const char* id,std::string label,bool selected){
        int r=row++;
        if(r<listScroll_ || r>=listScroll_+rows) return false;
        ImGui::SetCursorScreenPos(at(x+4,ly));
        bool clicked=!s.building && ImGui::InvisibleButton(id,{(w-14)*scale_,10*scale_});
        if(s.building) ImGui::Dummy({(w-14)*scale_,10*scale_});
        bool hovered=!s.building && ImGui::IsItemHovered();
        if(selected) rect(x+4,ly,w-8,10,navy);
        else if(hovered) rect(x+4,ly,w-8,10,rgb(0x1D2B53,140));
        if(selected && blink) text(x+6,ly+2,"\x01",yellow);
        label=upper(label); if(label.size()>18) label=label.substr(0,17)+".";
        text(x+14,ly+2,label,selected?yellow:(hovered?white:silver));
        ly+=11;
        return clicked;
    };
    for(size_t i=0;i<programs_.size();++i) if(item(("##p"+std::to_string(i)).c_str(),programs_[i].label,!file_ && i==choice_)) choose(i);
    if(item("##file",file_?file_->filename().string():"OPEN FILE...",file_.has_value())) {if(auto f=openProgramDialog()) file_=f;}
    // More above or below.
    if(listScroll_>0) text(x+w-10,listTop+1,"\x02",orange);
    if(listScroll_+rows<count) text(x+w-10,listTop+(rows-1)*11+2,"\x03",orange);
    ly=listTop+rows*11;

    float by=ly+3;
    if(button("##build",x+4,by,w-8,14,s.ready?"REBUILD":"BUILD",green,black,!s.building)) buildChosen();
    by+=18;
    if(s.building) {
        // Indeterminate segmented bar and the current phase.
        rect(x+4,by,w-8,8,black);
        int lit=static_cast<int>(std::fmod(time_*10,24));
        for(int i=0;i<20;++i) if(std::abs(i-lit+2)<=2) rect(x+6+static_cast<float>(i)*6,by+2,5,4,sky); else rect(x+6+static_cast<float>(i)*6,by+2,5,4,navy);
        std::string phase=upper(s.progress); if(phase.size()>20) phase=phase.substr(0,20);
        text(x+6,by+11,phase,silver);
        if(button("##cancel",x+4,by+21,w-8,12,"CANCEL",red,white)) session_.cancel();
        by+=37;
    }
    if(!s.error.empty() || !error_.empty()) {
        // Wrapped error, 20 characters per line, up to 4 lines.
        std::string e=upper(error_.empty()?s.error:error_);
        for(int line=0;line<4 && !e.empty();++line) {text(x+6,by,e.substr(0,20),red); e=e.size()>20?e.substr(20):""; by+=9;}
        by+=2;
    }
    if(!s.ready) return;

    text(x+6,by,"RUN",yellow); by+=10;
    if(button("##play",x+4,by,52,14,s.playing?"PAUSE":"PLAY",s.playing?orange:sky,black)) session_.setPlaying(!s.playing);
    if(button("##step",x+58,by,34,14,"STEP",silver,black)) session_.step();
    if(button("##reset",x+94,by,34,14,"RESET",silver,black)) session_.reset();
    by+=20;
    // Speed bar: more lit blocks run faster.
    int current=0; for(int i=0;i<8;++i) if(speeds[i]>=s.intervalMs) current=i;
    text(x+6,by,"SPEED",yellow);
    char value[16]; if(s.intervalMs==0) std::snprintf(value,sizeof value,"MAX"); else std::snprintf(value,sizeof value,"%dMS",s.intervalMs);
    text(x+w-6-static_cast<float>(pixelfont::width(value)),by,value,silver);
    by+=10;
    for(int i=0;i<8;++i) {
        float sx=x+6+static_cast<float>(i)*15;
        ImGui::SetCursorScreenPos(at(sx,by));
        if(ImGui::InvisibleButton(("##speed"+std::to_string(i)).c_str(),{14*scale_,10*scale_})) session_.setInterval(speeds[i]);
        bool hovered=ImGui::IsItemHovered();
        rect(sx,by,14,10,black);
        ImU32 c=i<=current?(i<4?green:i<7?yellow:red):(hovered?ash:navy);
        rect(sx+1,by+1+static_cast<float>(7-i),12,9-static_cast<float>(7-i),c);
    }
}
void PixelApp::screen(const ims::MachineSession::Status& s,float x,float y,float w,float h) {
    bool blink=std::fmod(time_,1.0)<0.6;
    if(!s.ready) {
        if(s.building) {
            int dots=static_cast<int>(std::fmod(time_*3,4));
            textCentered(x+w/2,y+h/2-8,"BUILDING"+std::string(static_cast<size_t>(dots),'.'),sky,2);
            textCentered(x+w/2,y+h/2+12,"LAYING OUT MINES AND NUMBERS",ash);
        } else if(blink) textCentered(x+w/2,y+h/2-4,"PICK A PROGRAM AND PRESS BUILD",silver);
        return;
    }
    const int dw=s.displayWidth,dh=s.displayHeight;
    float dot=std::floor(std::min((w-10)/static_cast<float>(dw),(h-10)/static_cast<float>(dh)));
    dot=std::clamp(dot,2.0f,24.0f);
    float gw=dot*static_cast<float>(dw),gh=dot*static_cast<float>(dh),gx=std::floor(x+(w-gw)/2),gy=std::floor(y+(h-gh)/2);
    // Bezel and screen.
    rect(gx-4,gy-4,gw+8,gh+8,black); rect(gx-3,gy-3,gw+6,gh+6,ash); rect(gx-2,gy-2,gw+4,gh+4,black);
    for(int j=0;j<dh;++j) for(int i=0;i<dw;++i) {
        float px=gx+static_cast<float>(i)*dot,py=gy+static_cast<float>(j)*dot;
        if(s.display[static_cast<size_t>(i+dw*j)]) {
            // A lit block with a bevel, like a sprite tile.
            rect(px,py,dot,dot,sky);
            if(dot>=4) {rect(px,py,dot,1,white); rect(px,py,1,dot,rgb(0xFFF1E8,160)); rect(px,py+dot-1,dot,1,navy); rect(px+dot-1,py,1,dot,navy);}
        } else if(dot>=4) rect(px+std::floor(dot/2)-0.5f,py+std::floor(dot/2)-0.5f,1,1,navy);
    }
}
void PixelApp::board(float x,float y,float w,float h) {
    // Controls on the panel's top edge.
    if(button("##follow",x+w-80,y+2,48,11,camera_.follow?"FOLLOW":"FREE",camera_.follow?green:silver,black)) {camera_.follow=!camera_.follow; camera_.followed=-1;}
    if(button("##fit",x+w-30,y+2,28,11,"FIT",silver,black)) camera_.fit();
    float top=y+16;
    ImVec2 p0=at(x,top),size{w*scale_,(h-16)*scale_};
    ImGui::SetCursorScreenPos(p0);
    ImGui::InvisibleButton("##map",size);
    const bool hovered=ImGui::IsItemHovered(),active=ImGui::IsItemActive();
    draw_->AddRectFilled(p0,{p0.x+size.x,p0.y+size.y},black);
    draw_->PushClipRect(p0,{p0.x+size.x,p0.y+size.y},true);
    session_.withMachine([&](const ims::FrameMachine& m){
        using Cell=ims::FrameMachine::Cell;
        camera_.show(m,p0,size,hovered,active,[&](Cell c,int digit,float ax,float ay,float e,bool wire){drawClassicCell(draw_,c,digit,ax,ay,e,wire);
        },[&](float bx,float t,float b){draw_->AddRectFilled({bx,t},{bx+std::max(1.0f,scale_/2),b},yellow);});
    });
    draw_->PopClipRect();
    if(!session_.status().ready) textCentered(x+w/2,y+h/2,"NO BOARD YET",ash);
}
void PixelApp::status(const ims::MachineSession::Status& s) {
    const float x=portrait_?4.0f:140.0f,y=192,w=256,h=54;
    panel(x,y,w,h,"STATUS");
    if(!s.ready) {text(x+8,y+12,s.building?"PLEASE WAIT":"READY",silver); return;}
    std::string name=upper(s.name); if(name.size()>16) name=name.substr(0,16);
    text(x+8,y+10,name,yellow);
    char line[40];
    std::snprintf(line,sizeof line,"TICK %06lld",static_cast<long long>(s.ticks)); text(x+8,y+20,line,white);
    std::snprintf(line,sizeof line,"SOLVE %.0fMS",s.solveMs); text(x+8,y+29,line,silver);
    std::snprintf(line,sizeof line,"NOR %zu",s.gates); text(x+8,y+38,line,silver);
    // Key caps: lit while held, or when the last tick used the key.
    auto binds=bindingsFor(s.keys);
    const float capW=22,gap=2; float cx=x+w-6-static_cast<float>(s.keys.size())*(capW+gap)+gap;
    for(size_t i=0;i<s.keys.size();++i) {
        // A key cap is also a button: holding it down holds the key.
        if(capCenters_.size()<s.keys.size()) capCenters_.resize(s.keys.size());
        capCenters_[i]=at(cx+capW/2,y+18);
        ImGui::SetCursorScreenPos(at(cx,y+10)); ImGui::InvisibleButton(("##cap"+std::to_string(i)).c_str(),{capW*scale_,17*scale_});
        if(capHeld_.size()<s.keys.size()) capHeld_.resize(s.keys.size(),0);
        capHeld_[i]=ImGui::IsItemActive();
        bool held=capHeld_[i]!=0; for(auto k:binds[i]) held|=ImGui::IsKeyDown(k);
        bool used=i<s.lastKeys.size() && s.lastKeys[i];
        if(!held && ImGui::IsItemHovered()) rect(cx-1,y+9,capW+2,19,sky);
        float o=held?1.0f:0.0f;
        if(!held) rect(cx+1,y+11,capW,17,black);
        rect(cx+o,y+10+o,capW,17,black);
        rect(cx+o+1,y+11+o,capW-2,15,held?yellow:used?orange:silver);
        rect(cx+o+1,y+11+o,capW-2,1,white);
        textCentered(cx+o+capW/2,y+15+o,capFor(binds[i]),black);
        std::string label=upper(s.keys[i]); if(label.size()>3) label=label.substr(0,3);
        textCentered(cx+capW/2,y+31,label,s.keyPress[i]?pink:silver);
        cx+=capW+gap;
    }
}
void PixelApp::code(float x,float y,float w,float h) {
    // Toolbar: new, samples, files, the reference and RUN; the name is on the bottom line.
    float bx=x+1;
    auto next=[&](float width){float at_=bx; bx+=width+2; return at_;};
    if(button("##new",next(26),y+1,26,12,"NEW",silver,black)) newCode(newProgram);
    if(button("##samples",next(46),y+1,46,12,"SAMPLES",samples_?yellow:orange,black)) samples_=!samples_;
    if(button("##open",next(30),y+1,30,12,"OPEN",silver,black)) {if(auto f=openProgramDialog()) openCodeFile(*f);}
    if(button("##save",next(30),y+1,30,12,"SAVE",silver,black)) {
        auto target=codePath_?codePath_:saveProgramDialog(codeName_+(codeLanguage_==ims::ProgramLanguage::Imsg?".imsg":".mc"));
        if(target) {
            try {writeTextFile(*target,editor_.text()); codePath_=target; codeName_=target->stem().string(); dirty_=false;}
            catch(const std::exception& e) {error_=e.what();}
        }
    }
    if(button("##ref",next(26),y+1,26,12,"REF",sky,black) && docs_) docs_->open();
    bx=x+w-42;
    bool run=button("##run",bx,y+1,42,12,"RUN \x05",green,black,!session_.status().building);
    // The editor: pixel font at a whole-number scale, highlighted, with completion.
    const float top=y+16,bottom=y+h-11;
    const float fontScale=std::max(1.0f,scale_-1);
    if(samples_) samplePicker(x,top,w,bottom-top);
    else if(editor_.draw("##editor",at(x,top),{w*scale_,(bottom-top)*scale_},fontScale,pico8Code(),errorLine_)) {dirty_=true; errorLine_=0;}
    // Bottom line: the compile error, or the program's name and the keys.
    if(!error_.empty()) {std::string e=upper(error_); if(e.size()>40) e=e.substr(0,39)+"."; text(x+3,y+h-8,e,red);}
    else {
        std::string title=upper(codePath_?codePath_->filename().string():codeName_)+(dirty_?" *":"");
        if(title.size()>18) title=title.substr(0,17)+".";
        text(x+3,y+h-8,title,yellow);
        text(x+3+static_cast<float>(pixelfont::width(title))+8,y+h-8,codeLanguage_==ims::ProgramLanguage::Imsg?".IMSG  F5 RUN":"F5 RUN  CTRL+SPACE COMPLETE",ash);
    }
    if(samples_ && ImGui::IsKeyPressed(ImGuiKey_Escape)) samples_=false;
    if(run || ImGui::IsKeyPressed(ImGuiKey_F5,false)) runCode();
}
void PixelApp::samplePicker(float x,float y,float w,float h) {
    // One card per sample: its name, what it is and its size; a click opens it.
    static const std::pair<const char*,const char*> blurbs[]={
        {"Blink","A LIGHT ON AND OFF"},{"Dot","ARROW KEYS MOVE IT"},{"Binary","COUNT IN 8 BITS"},{"Bounce","A BALL ON ITS OWN"},
        {"Smiley","A PICTURE IN ROWS"},{"Dice","ROLL 1 TO 6"},
        {"Tetris","FALLING BLOCKS"},{"Breakout","PADDLE AND BRICKS"},{"Invaders","SHOOT THE INVADERS"},{"Flappy","FLAP THROUGH PIPES"},
        {"Snake","EAT AND GROW, 8X8"},{"Pong","BOUNCE THE BALL"},{"Catch","CATCH THE BALL"},{"Paint","DRAW PIXELS, 6X6"},
        {"Life","GAME OF LIFE, 8X8"},{"Langton's Ant","THE ANT'S HIGHWAY"},{"Automata","RULES 30, 90, 110"},{"Mandelbrot","ONE STEP PER TICK"},{"Counter","4-DIGIT COUNTER"}};
    if(sampleGates_.size()!=programs_.size()) {
        sampleGates_.clear();
        for(const auto& p:programs_) {try {sampleGates_.push_back(p.compile().net.gates.size());} catch(const std::exception&) {sampleGates_.push_back(0);}}
    }
    rect(x,y,w,h,panelFill);
    text(x+3,y+1,"PICK A SAMPLE  (ESC: BACK)",ash);
    // Two columns of cards; the wheel scrolls when they do not all fit.
    const float cw=(w-6)/2,ch=21,top=y+10,pitch=ch+2;
    const int rowsShown=std::max(1,static_cast<int>((h-10)/pitch)),rowCount=static_cast<int>((programs_.size()+1)/2);
    const ImVec2 m=ImGui::GetIO().MousePos;
    if(m.x>=at(x,top).x && m.x<at(x+w,top).x && m.y>=at(x,top).y && m.y<at(x,y+h).y) pickScroll_-=static_cast<int>(ImGui::GetIO().MouseWheel);
    pickScroll_=std::clamp(pickScroll_,0,std::max(0,rowCount-rowsShown));
    if(pickScroll_>0) text(x+w-8,y+1,"\x02",orange);
    if(pickScroll_+rowsShown<rowCount) text(x+w-16,y+1,"\x03",orange);
    for(size_t i=0;i<programs_.size();++i) {
        int r=static_cast<int>(i/2)-pickScroll_;
        if(r<0 || r>=rowsShown) continue;
        float cx=x+2+static_cast<float>(i%2)*(cw+2),cy=top+static_cast<float>(r)*pitch;
        ImGui::SetCursorScreenPos(at(cx,cy));
        bool hit=ImGui::InvisibleButton(("##sample"+std::to_string(i)).c_str(),{cw*scale_,ch*scale_});
        bool hov=ImGui::IsItemHovered(),chosen=!file_ && i==choice_;
        rect(cx,cy,cw,ch,black); rect(cx+1,cy+1,cw-2,ch-2,hov?navy:chosen?rgb(0x1D2B53,160):rgb(0x05070F));
        if(hov) {rect(cx,cy,cw,1,yellow); rect(cx,cy+ch-1,cw,1,yellow); rect(cx,cy,1,ch,yellow); rect(cx+cw-1,cy,1,ch,yellow);}
        // Text fits the card: the size only when there is room beside the name.
        const size_t room=static_cast<size_t>((cw-8)/pixelfont::advance);
        std::string name=upper(programs_[i].label); if(name.size()>room) name=name.substr(0,room);
        text(cx+4,cy+3,name,hov || chosen?yellow:white);
        char gates[24]; std::snprintf(gates,sizeof gates,"%zu NOR",sampleGates_[i]);
        if(name.size()+std::string(gates).size()+1<=room) text(cx+cw-4-static_cast<float>(pixelfont::width(gates)),cy+3,gates,ash);
        std::string blurb;
        for(const auto& [n,b]:blurbs) if(programs_[i].label==n) blurb=b;
        if(blurb.size()>room) blurb=blurb.substr(0,room);
        text(cx+4,cy+12,blurb,silver);
        if(hit) {choice_=i; file_.reset(); loadCode(i); samples_=false; editor_.focus();}
    }
}
}
