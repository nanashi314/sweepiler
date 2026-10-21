// The reference in the browser: no second window, so it covers the lab in the
// same canvas and ImGui context until BACK, Esc or OPEN IN EDITOR.
#include "PixelDocs.h"
#include "PixelFont.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
namespace lab {
PixelDocs::~PixelDocs()=default;
void PixelDocs::open() {window_=this;}
long long PixelDocs::handle(void*,unsigned,unsigned long long,long long) {return 0;}
void PixelDocs::close() {window_=nullptr; closing_=false;}
void PixelDocs::render() {
    if(!window_) return;
    if(closing_) {close(); return;}
    const auto& io=ImGui::GetIO();
    const float width=io.DisplaySize.x,height=io.DisplaySize.y;
    bool opened=false; auto user=openSample;
    openSample=[&](const std::string& name){opened=true; if(user) user(name);};
    ui(width,height);
    openSample=user;
    // BACK at the right of the tree's title, sized like the reference's own
    // buttons; where the tree is too narrow for both (phones), at its foot.
    const float v=width>=1500 && height>=900?3.0f:2.0f,pad=6*v,treeW=treeWidth(width,v);
    const float w=30*v,h=11*v,x=treeW>=110*v?pad+treeW-38*v:pad+(treeW-w)/2,y=treeW>=110*v?pad+4*v:height-pad-h-6*v;
    ImGui::SetNextWindowPos({x,y}); ImGui::SetNextWindowSize({w+v,h+v});
    ImGui::Begin("##docsback",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBackground);
    // A click in the reference brings its window to the front; BACK stays above it.
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    auto* d=ImGui::GetWindowDrawList();
    bool back=ImGui::InvisibleButton("##back",{w,h}),hov=ImGui::IsItemHovered(),down=ImGui::IsItemActive();
    const float o=down?v:0;
    if(!down) d->AddRectFilled({x+v,y+v},{x+v+w,y+v+h},IM_COL32(0,0,0,255));
    d->AddRectFilled({x+o,y+o},{x+o+w,y+o+h},IM_COL32(0,0,0,255));
    d->AddRectFilled({x+o+v,y+o+v},{x+o+w-v,y+o+h-v},hov?IM_COL32(255,241,232,255):IM_COL32(255,163,0,255));
    pixelfont::draw(d,{x+o+(w-static_cast<float>(pixelfont::width("BACK"))*v)/2,y+o+(h-7*v)/2},v,IM_COL32(0,0,0,255),"BACK");
    ImGui::End();
    if(back || opened || ImGui::IsKeyPressed(ImGuiKey_Escape,false)) close();
}
}
