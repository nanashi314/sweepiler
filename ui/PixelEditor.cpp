#include "PixelEditor.h"
#include "LabCommon.h"
#include "PixelFont.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <set>
namespace lab {
namespace {
constexpr ImU32 rgb(unsigned v,unsigned a=255) {return IM_COL32((v>>16)&255,(v>>8)&255,v&255,a);}
bool identStart(char c) {return std::isalpha(static_cast<unsigned char>(c)) || c=='_';}
bool identChar(char c) {return std::isalnum(static_cast<unsigned char>(c)) || c=='_';}
int charLen(unsigned char b) {return b<0x80?1:b>=0xF0?4:b>=0xE0?3:b>=0xC0?2:1;}
bool isType(std::string_view w) {
    if(w=="bool" || w=="int") return true;
    auto sized=[&](std::string_view p){if(w.size()<=p.size() || w.substr(0,p.size())!=p) return false; for(char c:w.substr(p.size())) if(!std::isdigit(static_cast<unsigned char>(c))) return false; return true;};
    return sized("uint") || sized("int");
}
const std::set<std::string,std::less<>> keywords{"if","else","for","while","return","break","continue","void","const","input","hold","press","true","false"};
}
const CodeColors& pico8Code() {
    static const CodeColors c{
        rgb(0x05070F),rgb(0xFFF1E8),rgb(0xFF77A8),rgb(0x29ADFF),rgb(0xFFA300),rgb(0xFFCCAA),rgb(0x83769C),rgb(0xFFEC27),rgb(0x00E436),rgb(0xC2C3C7),
        rgb(0x0B1020),rgb(0x5F574F),rgb(0x1D2B53,150),rgb(0x7E2553,210),rgb(0xFFEC27),rgb(0xFF004D,170),rgb(0x29ADFF,150),
        rgb(0x1D2B53),rgb(0xC2C3C7),rgb(0x7E2553),rgb(0xFFF1E8),rgb(0xFFA300),rgb(0x83769C)};
    return c;
}
void classify(std::string_view s,bool& inBlock,std::vector<Tok>& out) {
    out.assign(s.size(),Tok::Text);
    size_t i=0;
    while(i<s.size()) {
        if(inBlock) {
            size_t e=s.find("*/",i);
            size_t end=e==std::string_view::npos?s.size():e+2;
            for(size_t k=i;k<end;++k) out[k]=Tok::Comment;
            if(e!=std::string_view::npos) inBlock=false;
            i=end; continue;
        }
        char c=s[i];
        if(c=='/' && i+1<s.size() && s[i+1]=='/') {for(size_t k=i;k<s.size();++k) out[k]=Tok::Comment; break;}
        if(c=='/' && i+1<s.size() && s[i+1]=='*') {inBlock=true; continue;}
        if(c=='#') {size_t e=i+1; while(e<s.size() && identChar(s[e])) ++e; for(size_t k=i;k<e;++k) out[k]=Tok::Directive; i=e; continue;}
        if(std::isdigit(static_cast<unsigned char>(c))) {size_t e=i; while(e<s.size() && identChar(s[e])) ++e; for(size_t k=i;k<e;++k) out[k]=Tok::Number; i=e; continue;}
        if(identStart(c)) {
            size_t e=i; while(e<s.size() && identChar(s[e])) ++e;
            std::string_view w=s.substr(i,e-i); size_t n=e; while(n<s.size() && s[n]==' ') ++n;
            Tok t=keywords.contains(w)?Tok::Keyword:isType(w)?Tok::Type:(w=="pixel" || w=="at")?Tok::Builtin:(n<s.size() && s[n]=='(')?Tok::Function:Tok::Text;
            for(size_t k=i;k<e;++k) out[k]=t;
            i=e; continue;
        }
        if(std::strchr("+-*/%&|^~!<>=?:;,(){}[]",c)) out[i]=Tok::Punct;
        i+=static_cast<size_t>(charLen(static_cast<unsigned char>(c)));
    }
}
void drawCodeLine(ImDrawList* d,ImVec2 p,float scale,std::string_view line,bool& inBlock,const CodeColors& c) {
    std::vector<Tok> tok; classify(line,inBlock,tok);
    auto colour=[&](Tok t){switch(t){case Tok::Keyword: return c.keyword; case Tok::Type: return c.type; case Tok::Builtin: return c.builtin; case Tok::Number: return c.number;
        case Tok::Comment: return c.comment; case Tok::Directive: return c.directive; case Tok::Function: return c.function; case Tok::Punct: return c.punct; default: return c.text;}};
    const float cw=6*scale; float x=p.x; const float y=p.y+1.5f*scale;
    for(size_t i=0;i<line.size();) {
        int n=charLen(static_cast<unsigned char>(line[i]));
        if(n==1) {char t[2]={line[i],0}; if(line[i]!=' ') pixelfont::draw(d,{x,y},scale,colour(tok[i]),t); x+=cw;}
        else {
            // Beyond ASCII: a TrueType glyph over two cells.
            d->AddText(jpFont(),8.5f*scale,{x,p.y+0.5f*scale},colour(tok[i]),line.data()+i,line.data()+std::min(line.size(),i+static_cast<size_t>(n)));
            x+=2*cw;
        }
        i+=static_cast<size_t>(n);
    }
}
// ---------------- editor
void PixelEditor::setText(const std::string& text) {
    lines_.clear(); std::string cur;
    for(char ch:text) {
        if(ch=='\r') continue;
        if(ch=='\n') {lines_.push_back(cur); cur.clear();}
        else if(ch=='\t') cur+="    ";
        else cur+=ch;
    }
    lines_.push_back(cur);
    cursor_=anchor_={}; top_=leftCell_=0; undo_.clear(); redo_.clear(); popup_=false; lastKind_=-1;
}
std::string PixelEditor::text() const {
    std::string s; for(size_t i=0;i<lines_.size();++i) {s+=lines_[i]; if(i+1<lines_.size()) s+='\n';}
    return s;
}
std::pair<PixelEditor::Pos,PixelEditor::Pos> PixelEditor::ordered() const {
    bool before=anchor_.line<cursor_.line || (anchor_.line==cursor_.line && anchor_.col<cursor_.col);
    return before?std::pair{anchor_,cursor_}:std::pair{cursor_,anchor_};
}
std::string PixelEditor::selected() const {
    auto [a,b]=ordered(); if(a.line==b.line) return lines_[static_cast<size_t>(a.line)].substr(static_cast<size_t>(a.col),static_cast<size_t>(b.col-a.col));
    std::string s=lines_[static_cast<size_t>(a.line)].substr(static_cast<size_t>(a.col))+"\n";
    for(int l=a.line+1;l<b.line;++l) s+=lines_[static_cast<size_t>(l)]+"\n";
    return s+lines_[static_cast<size_t>(b.line)].substr(0,static_cast<size_t>(b.col));
}
void PixelEditor::remember(int kind) {
    double now=ImGui::GetTime();
    if(kind!=lastKind_ || kind==2 || now-lastEdit_>1.0) {undo_.push_back({lines_,cursor_}); if(undo_.size()>300) undo_.erase(undo_.begin());}
    redo_.clear(); lastKind_=kind; lastEdit_=now;
}
void PixelEditor::eraseSelection() {
    if(!hasSelection()) return;
    auto [a,b]=ordered();
    auto& first=lines_[static_cast<size_t>(a.line)];
    first=first.substr(0,static_cast<size_t>(a.col))+lines_[static_cast<size_t>(b.line)].substr(static_cast<size_t>(b.col));
    lines_.erase(lines_.begin()+a.line+1,lines_.begin()+b.line+1);
    cursor_=anchor_=a; changed_=true;
}
void PixelEditor::insert(const std::string& s) {
    eraseSelection();
    auto& line=lines_[static_cast<size_t>(cursor_.line)];
    std::string tail=line.substr(static_cast<size_t>(cursor_.col)); line.resize(static_cast<size_t>(cursor_.col));
    size_t start=0; int l=cursor_.line;
    for(;;) {
        size_t nl=s.find('\n',start);
        lines_[static_cast<size_t>(l)]+=s.substr(start,nl==std::string::npos?std::string::npos:nl-start);
        if(nl==std::string::npos) break;
        lines_.insert(lines_.begin()+l+1,std::string()); ++l; start=nl+1;
    }
    cursor_={l,static_cast<int>(lines_[static_cast<size_t>(l)].size())};
    lines_[static_cast<size_t>(l)]+=tail; anchor_=cursor_; changed_=true; wantCell_=-1;
}
void PixelEditor::moveTo(Pos p,bool select) {cursor_=p; if(!select) anchor_=p;}
PixelEditor::Pos PixelEditor::left(Pos p) const {
    if(p.col>0) {const auto& s=lines_[static_cast<size_t>(p.line)]; int c=p.col-1; while(c>0 && (static_cast<unsigned char>(s[static_cast<size_t>(c)])&0xC0)==0x80) --c; return {p.line,c};}
    if(p.line>0) return {p.line-1,static_cast<int>(lines_[static_cast<size_t>(p.line-1)].size())};
    return p;
}
PixelEditor::Pos PixelEditor::right(Pos p) const {
    const auto& s=lines_[static_cast<size_t>(p.line)];
    if(p.col<static_cast<int>(s.size())) return {p.line,p.col+charLen(static_cast<unsigned char>(s[static_cast<size_t>(p.col)]))};
    if(p.line+1<static_cast<int>(lines_.size())) return {p.line+1,0};
    return p;
}
PixelEditor::Pos PixelEditor::wordLeft(Pos p) const {
    if(p.col==0) return left(p);
    const auto& s=lines_[static_cast<size_t>(p.line)]; int c=p.col;
    while(c>0 && s[static_cast<size_t>(c-1)]==' ') --c;
    if(c>0 && identChar(s[static_cast<size_t>(c-1)])) while(c>0 && identChar(s[static_cast<size_t>(c-1)])) --c; else if(c>0) --c;
    return {p.line,c};
}
PixelEditor::Pos PixelEditor::wordRight(Pos p) const {
    const auto& s=lines_[static_cast<size_t>(p.line)]; int c=p.col,n=static_cast<int>(s.size());
    if(c>=n) return right(p);
    if(identChar(s[static_cast<size_t>(c)])) while(c<n && identChar(s[static_cast<size_t>(c)])) ++c; else ++c;
    while(c<n && s[static_cast<size_t>(c)]==' ') ++c;
    return {p.line,c};
}
int PixelEditor::column(Pos p) const {
    const auto& s=lines_[static_cast<size_t>(p.line)]; int cells=0;
    for(int i=0;i<p.col && i<static_cast<int>(s.size());) {int n=charLen(static_cast<unsigned char>(s[static_cast<size_t>(i)])); cells+=n==1?1:2; i+=n;}
    return cells;
}
int PixelEditor::colAtCell(int line,int cell) const {
    const auto& s=lines_[static_cast<size_t>(line)]; int cells=0,i=0;
    while(i<static_cast<int>(s.size())) {int n=charLen(static_cast<unsigned char>(s[static_cast<size_t>(i)])),w=n==1?1:2; if(cells+w>cell) break; cells+=w; i+=n;}
    return i;
}
std::string PixelEditor::wordBefore() const {
    const auto& s=lines_[static_cast<size_t>(cursor_.line)]; int c=cursor_.col;
    while(c>0 && identChar(s[static_cast<size_t>(c-1)])) --c;
    if(c<cursor_.col && !identStart(s[static_cast<size_t>(c)])) return {};
    return s.substr(static_cast<size_t>(c),static_cast<size_t>(cursor_.col-c));
}
void PixelEditor::updateCompletion(bool force) {
    std::string prefix=wordBefore();
    if(prefix.empty() && !force) {popup_=false; return;}
    static const std::vector<Item> fixed={
        {"if","if ($) {\n}","if (condition) { ... }",'S'},{"else","else ","",'K'},{"for","for (int i = 0; i < $; i++) {\n}","for with constant bounds (unrolled)",'S'},
        {"return","return $;","",'K'},{"break","break;","leave the loop",'K'},{"continue","continue;","next iteration",'K'},
        {"void","void ","",'K'},{"const","const int $ = ;","compile-time constant",'K'},{"true","true","",'K'},{"false","false","",'K'},
        {"input","input hold $;","a key: hold (while held) or press (once per press)",'K'},{"hold","hold ","",'K'},{"press","press ","",'K'},
        {"tick","void tick() {\n    $\n}","runs once per tick",'S'},{"draw","void draw() {\n    $\n}","draws the screen after tick",'S'},
        {"pixel","pixel($)","pixel(x, y, v): light a screen pixel in draw()",'F'},{"at","at($)","at(x, y): placement hint for the next gates",'F'},
        {"bool","bool ","1 bit",'T'},{"int","int ","32-bit signed",'T'},{"uint2","uint2 ","",'T'},{"uint3","uint3 ","",'T'},{"uint4","uint4 ","",'T'},
        {"uint5","uint5 ","",'T'},{"uint6","uint6 ","",'T'},{"uint8","uint8 ","",'T'},{"uint16","uint16 ","",'T'},{"int4","int4 ","",'T'},{"int5","int5 ","",'T'},
        {"int8","int8 ","",'T'},{"int16","int16 ","",'T'}};
    items_.clear(); std::set<std::string> seen;
    for(const auto& it:fixed) if(it.label.compare(0,prefix.size(),prefix)==0 && it.label!=prefix+"") {items_.push_back(it); seen.insert(it.label);}
    for(const auto& it:fixed) if(it.label==prefix && it.kind=='S') {items_.insert(items_.begin(),it); seen.insert(it.label);}
    // Names already in the program (outside comments).
    bool block=false; std::vector<Tok> tok;
    for(int l=0;l<static_cast<int>(lines_.size());++l) {
        const auto& s=lines_[static_cast<size_t>(l)]; classify(s,block,tok);
        for(size_t i=0;i<s.size();) {
            if(identStart(s[i]) && (tok[i]==Tok::Text || tok[i]==Tok::Function)) {
                size_t e=i; while(e<s.size() && identChar(s[e])) ++e;
                std::string w=s.substr(i,e-i);
                bool atCursor=l==cursor_.line && static_cast<int>(e)==cursor_.col;
                if(!atCursor && w.size()>=2 && w!=prefix && w.compare(0,prefix.size(),prefix)==0 && seen.insert(w).second)
                    items_.push_back({w,tok[i]==Tok::Function?w+"($)":w,tok[i]==Tok::Function?"function":"",'N'});
                i=e;
            } else ++i;
        }
    }
    popup_=!items_.empty(); pick_=std::clamp(pick_,0,std::max(0,static_cast<int>(items_.size())-1));
    if(!popup_) pick_=0;
}
void PixelEditor::accept() {
    if(items_.empty()) {popup_=false; return;}
    const Item it=items_[static_cast<size_t>(pick_)];
    remember(2);
    std::string prefix=wordBefore();
    anchor_={cursor_.line,cursor_.col-static_cast<int>(prefix.size())};
    // Snippets: '$' marks where the cursor goes; new lines keep the indent.
    const auto& line=lines_[static_cast<size_t>(cursor_.line)];
    std::string indent; for(char ch:line) {if(ch!=' ') break; indent+=' ';}
    std::string ins; for(char ch:it.insert) {ins+=ch; if(ch=='\n') ins+=indent;}
    size_t mark=ins.find('$'); std::string before=mark==std::string::npos?ins:ins.substr(0,mark),after=mark==std::string::npos?"":ins.substr(mark+1);
    insert(before); Pos at=cursor_; insert(after); cursor_=anchor_=at;
    popup_=false;
}
bool PixelEditor::keyboard() {
    auto& io=ImGui::GetIO(); const bool ctrl=io.KeyCtrl,shift=io.KeyShift;
    auto key=[](ImGuiKey k){return ImGui::IsKeyPressed(k,true);};
    bool moved=false;
    if(popup_) {
        if(key(ImGuiKey_DownArrow)) {pick_=(pick_+1)%static_cast<int>(items_.size()); return true;}
        if(key(ImGuiKey_UpArrow)) {pick_=(pick_+static_cast<int>(items_.size())-1)%static_cast<int>(items_.size()); return true;}
        if(key(ImGuiKey_Escape)) {popup_=false; return true;}
        if(key(ImGuiKey_Tab) || key(ImGuiKey_Enter) || key(ImGuiKey_KeypadEnter)) {accept(); return true;}
    }
    const int lineCount=static_cast<int>(lines_.size());
    auto vertical=[&](int dl){
        if(wantCell_<0) wantCell_=column(cursor_);
        int l=std::clamp(cursor_.line+dl,0,lineCount-1);
        moveTo({l,colAtCell(l,wantCell_)},shift); moved=true; popup_=false;
    };
    auto horizontal=[&](Pos p){if(!shift && hasSelection()) {auto [a,b]=ordered(); p=p.line<cursor_.line || (p.line==cursor_.line && p.col<cursor_.col)?a:b;} moveTo(p,shift); wantCell_=-1; moved=true; popup_=false;};
    if(key(ImGuiKey_LeftArrow)) horizontal(ctrl?wordLeft(cursor_):left(cursor_));
    if(key(ImGuiKey_RightArrow)) horizontal(ctrl?wordRight(cursor_):right(cursor_));
    if(key(ImGuiKey_UpArrow)) vertical(-1);
    if(key(ImGuiKey_DownArrow)) vertical(1);
    if(key(ImGuiKey_PageUp)) vertical(-20);
    if(key(ImGuiKey_PageDown)) vertical(20);
    if(key(ImGuiKey_Home)) {
        if(ctrl) moveTo({0,0},shift);
        else {const auto& s=lines_[static_cast<size_t>(cursor_.line)]; int f=0; while(f<static_cast<int>(s.size()) && s[static_cast<size_t>(f)]==' ') ++f; moveTo({cursor_.line,cursor_.col==f?0:f},shift);}
        wantCell_=-1; moved=true;
    }
    if(key(ImGuiKey_End)) {if(ctrl) moveTo({lineCount-1,static_cast<int>(lines_.back().size())},shift); else moveTo({cursor_.line,static_cast<int>(lines_[static_cast<size_t>(cursor_.line)].size())},shift); wantCell_=-1; moved=true;}
    if(ctrl && key(ImGuiKey_A)) {anchor_={0,0}; cursor_={lineCount-1,static_cast<int>(lines_.back().size())}; moved=true;}
    if(ctrl && (key(ImGuiKey_C) || key(ImGuiKey_X)) && hasSelection()) {
        ImGui::SetClipboardText(selected().c_str());
        if(ImGui::IsKeyPressed(ImGuiKey_X)) {remember(2); eraseSelection();}
    }
    if(ctrl && key(ImGuiKey_V)) {
        if(const char* clip=ImGui::GetClipboardText()) {
            std::string t; for(const char* p=clip;*p;++p) {if(*p=='\r') continue; if(*p=='\t') t+="    "; else t+=*p;}
            remember(2); insert(t);
        }
        moved=true;
    }
    if(ctrl && key(ImGuiKey_Z) && !shift) {if(!undo_.empty()) {redo_.push_back({lines_,cursor_}); lines_=undo_.back().lines; cursor_=anchor_=undo_.back().cursor; undo_.pop_back(); changed_=true; lastKind_=-1;} moved=true;}
    if(ctrl && (key(ImGuiKey_Y) || (shift && key(ImGuiKey_Z)))) {if(!redo_.empty()) {undo_.push_back({lines_,cursor_}); lines_=redo_.back().lines; cursor_=anchor_=redo_.back().cursor; redo_.pop_back(); changed_=true; lastKind_=-1;} moved=true;}
    if(ctrl && key(ImGuiKey_Space)) {updateCompletion(true); return true;}
    if(ctrl && key(ImGuiKey_D)) {remember(2); lines_.insert(lines_.begin()+cursor_.line+1,lines_[static_cast<size_t>(cursor_.line)]); ++cursor_.line; anchor_=cursor_; changed_=true; moved=true;}
    if(key(ImGuiKey_Backspace)) {
        remember(1);
        if(hasSelection()) eraseSelection();
        else {
            const auto& s=lines_[static_cast<size_t>(cursor_.line)];
            bool indent=cursor_.col>0 && s.find_first_not_of(' ')>=static_cast<size_t>(cursor_.col);
            Pos from=ctrl?wordLeft(cursor_):indent?Pos{cursor_.line,std::max(0,(cursor_.col-1)/4*4)}:left(cursor_);
            anchor_=from; eraseSelection();
        }
        updateCompletion(false); moved=true;
    }
    if(key(ImGuiKey_Delete)) {remember(1); if(!hasSelection()) anchor_=ctrl?wordRight(cursor_):right(cursor_); eraseSelection(); popup_=false; moved=true;}
    if(key(ImGuiKey_Enter) || key(ImGuiKey_KeypadEnter)) {
        remember(2);
        const auto& s=lines_[static_cast<size_t>(cursor_.line)];
        std::string indent; for(char ch:s) {if(ch!=' ') break; indent+=' ';}
        bool open=cursor_.col>0 && s[static_cast<size_t>(cursor_.col-1)]=='{';
        bool close=cursor_.col<static_cast<int>(s.size()) && s[static_cast<size_t>(cursor_.col)]=='}';
        if(open && close) {insert("\n"+indent+"    "); Pos at=cursor_; insert("\n"+indent); cursor_=anchor_=at;}
        else insert("\n"+indent+(open?"    ":""));
        popup_=false; moved=true;
    }
    if(key(ImGuiKey_Tab)) {
        remember(2);
        auto [a,b]=ordered();
        if(a.line!=b.line) {
            // Indent or outdent the selected lines.
            for(int l=a.line;l<=b.line;++l) {auto& s=lines_[static_cast<size_t>(l)]; if(shift) {size_t n=std::min<size_t>(4,s.find_first_not_of(' ')==std::string::npos?s.size():s.find_first_not_of(' ')); s.erase(0,n);} else s.insert(0,"    ");}
            anchor_={a.line,0}; cursor_={b.line,static_cast<int>(lines_[static_cast<size_t>(b.line)].size())}; changed_=true;
        } else if(shift) {auto& s=lines_[static_cast<size_t>(cursor_.line)]; size_t n=std::min<size_t>(4,s.find_first_not_of(' ')==std::string::npos?s.size():s.find_first_not_of(' ')); s.erase(0,n); cursor_.col=std::max(0,cursor_.col-static_cast<int>(n)); anchor_=cursor_; changed_=true;}
        else insert(std::string(static_cast<size_t>(4-column(cursor_)%4),' '));
        moved=true;
    }
    // Typed characters.
    if(!ctrl || io.KeyAlt) for(ImWchar ch:io.InputQueueCharacters) {
        if(ch<32 || ch==127) continue;
        std::string u;
        if(ch<0x80) u+=static_cast<char>(ch);
        else if(ch<0x800) {u+=static_cast<char>(0xC0|(ch>>6)); u+=static_cast<char>(0x80|(ch&0x3F));}
        else {u+=static_cast<char>(0xE0|(ch>>12)); u+=static_cast<char>(0x80|((ch>>6)&0x3F)); u+=static_cast<char>(0x80|(ch&0x3F));}
        remember(ch==' '?2:0);
        if(ch=='}') {
            // A closing brace on an empty line goes back one indent step.
            auto& s=lines_[static_cast<size_t>(cursor_.line)];
            if(!hasSelection() && s.find_first_not_of(' ')==std::string::npos && cursor_.col>=4) {s.erase(0,4); cursor_.col-=4; anchor_=cursor_;}
        }
        insert(u);
        if(identChar(static_cast<char>(ch)) && ch<0x80) updateCompletion(false); else popup_=false;
        moved=true;
    }
    return moved;
}
void PixelEditor::mouse(ImVec2 origin,float cw,float lh,bool hovered,bool clickedInside) {
    auto& io=ImGui::GetIO();
    auto at=[&]{
        int l=std::clamp(top_+static_cast<int>(std::floor((io.MousePos.y-origin.y)/lh)),0,static_cast<int>(lines_.size())-1);
        int cell=std::max(0,leftCell_+static_cast<int>(std::lround((io.MousePos.x-origin.x)/cw)));
        return Pos{l,colAtCell(l,cell)};
    };
    if(clickedInside) {
        popup_=false; wantCell_=-1;
        if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            Pos p=at(); const auto& s=lines_[static_cast<size_t>(p.line)]; int a=p.col,b=p.col;
            while(a>0 && identChar(s[static_cast<size_t>(a-1)])) --a; while(b<static_cast<int>(s.size()) && identChar(s[static_cast<size_t>(b)])) ++b;
            anchor_={p.line,a}; cursor_={p.line,b};
        } else {moveTo(at(),io.KeyShift); dragging_=true;}
    }
    if(dragging_) {
        if(ImGui::IsMouseDown(ImGuiMouseButton_Left)) moveTo(at(),true); else dragging_=false;
    }
    (void)hovered;
}
void PixelEditor::ensureVisible(int rows,int cols) {
    if(cursor_.line<top_) top_=cursor_.line;
    if(cursor_.line>=top_+rows) top_=cursor_.line-rows+1;
    int cell=column(cursor_);
    if(cell<leftCell_) leftCell_=std::max(0,cell-4);
    if(cell>=leftCell_+cols) leftCell_=cell-cols+4;
}
void PixelEditor::drawCompletion(ImDrawList* d,ImVec2 o,float cw,float lh,float scale,const CodeColors& c,ImVec2 clipMax) {
    if(!popup_ || items_.empty()) return;
    const int shown=std::min<int>(7,static_cast<int>(items_.size())),first=std::clamp(pick_-shown+1,0,static_cast<int>(items_.size())-shown);
    float w=24*cw,h=(static_cast<float>(shown)+1.2f)*lh+2*scale;
    float x=o.x+static_cast<float>(column(cursor_)-leftCell_-static_cast<int>(wordBefore().size()))*cw-cw,y=o.y+static_cast<float>(cursor_.line-top_+1)*lh+scale;
    x=std::min(x,clipMax.x-w-2*scale); if(y+h>clipMax.y) y-=h+lh+2*scale;
    d->AddRectFilled({x+scale,y+scale},{x+w+scale,y+h+scale},IM_COL32(0,0,0,255));
    d->AddRectFilled({x,y},{x+w,y+h},c.popupEdge); d->AddRectFilled({x+scale,y+scale},{x+w-scale,y+h-scale},c.popupBg);
    for(int i=0;i<shown;++i) {
        const auto& it=items_[static_cast<size_t>(first+i)]; float ry=y+scale+static_cast<float>(i)*lh;
        if(first+i==pick_) d->AddRectFilled({x+scale,ry},{x+w-scale,ry+lh},c.popupSelect);
        char k[2]={it.kind,0}; pixelfont::draw(d,{x+3*scale,ry+1.5f*scale},scale,c.popupKind,k);
        std::string label=it.label; if(label.size()>20) label=label.substr(0,19)+".";
        pixelfont::draw(d,{x+3*scale+2*cw,ry+1.5f*scale},scale,c.popupText,label);
    }
    // The chosen item's hint, or the keys.
    const auto& sel=items_[static_cast<size_t>(pick_)];
    std::string hint=sel.hint.empty()?"TAB: INSERT  ESC: CLOSE":sel.hint; if(hint.size()>36) hint=hint.substr(0,35)+".";
    pixelfont::draw(d,{x+3*scale,y+scale+static_cast<float>(shown)*lh+2.5f*scale},scale*0.67f>=1?std::floor(scale*0.67f):scale,c.popupHint,hint);
}
bool PixelEditor::draw(const char* id,ImVec2 pos,ImVec2 size,float scale,const CodeColors& c,int errorLine) {
    changed_=false;
    auto* d=ImGui::GetWindowDrawList();
    const float cw=6*scale,lh=10*scale;
    const int lineCount=static_cast<int>(lines_.size());
    int digits=1; for(int n=lineCount;n>=10;n/=10) ++digits;
    const float gutter=static_cast<float>(digits+2)*cw;
    const ImVec2 origin{pos.x+gutter+cw*0.5f,pos.y+2*scale};
    const int rows=std::max(1,static_cast<int>((size.y-4*scale)/lh)),cols=std::max(1,static_cast<int>((size.x-gutter-cw)/cw));
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id,size);
    const bool hovered=ImGui::IsItemHovered(),clicked=ImGui::IsItemClicked(ImGuiMouseButton_Left);
    if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)) focused_=hovered;
    bool moved=false;
    if(focused_) {mouse(origin,cw,lh,hovered,clicked); moved=keyboard() || clicked || dragging_;}
    if(hovered) {
        auto& io=ImGui::GetIO();
        if(io.KeyShift) leftCell_=std::max(0,leftCell_-static_cast<int>(io.MouseWheel*4));
        else top_-=static_cast<int>(io.MouseWheel*3);
    }
    if(moved) ensureVisible(rows,cols);
    top_=std::clamp(top_,0,std::max(0,lineCount-1));
    if(changed_) blink_=ImGui::GetTime();
    // Background, gutter, current line, error line.
    d->AddRectFilled(pos,{pos.x+size.x,pos.y+size.y},c.bg);
    d->AddRectFilled(pos,{pos.x+gutter,pos.y+size.y},c.gutterBg);
    d->PushClipRect(pos,{pos.x+size.x,pos.y+size.y},true);
    auto rowY=[&](int l){return origin.y+static_cast<float>(l-top_)*lh;};
    auto cellX=[&](int cell){return origin.x+static_cast<float>(cell-leftCell_)*cw;};
    if(focused_) d->AddRectFilled({pos.x+gutter,rowY(cursor_.line)},{pos.x+size.x,rowY(cursor_.line)+lh},c.currentLine);
    if(errorLine>=1 && errorLine<=lineCount) {
        d->AddRectFilled({pos.x,rowY(errorLine-1)},{pos.x+gutter-scale,rowY(errorLine-1)+lh},c.errorFill);
        d->AddRectFilled({pos.x+gutter,rowY(errorLine-1)},{pos.x+size.x,rowY(errorLine-1)+lh},IM_COL32(255,0,77,40));
    }
    // Selection.
    if(hasSelection()) {
        auto [a,b]=ordered();
        for(int l=std::max(a.line,top_);l<=std::min(b.line,top_+rows);++l) {
            int from=l==a.line?column(a):0,to=l==b.line?column(b):column({l,static_cast<int>(lines_[static_cast<size_t>(l)].size())})+1;
            d->AddRectFilled({cellX(from),rowY(l)},{cellX(to),rowY(l)+lh},c.selection);
        }
    }
    // Matching bracket.
    auto bracketAt=[&](Pos p)->int{const auto& s=lines_[static_cast<size_t>(p.line)]; return p.col>=0 && p.col<static_cast<int>(s.size())?s[static_cast<size_t>(p.col)]:0;};
    if(focused_) {
        Pos p=cursor_; int ch=bracketAt(p);
        if(!std::strchr("(){}[]",ch) || !ch) {p=left(cursor_); ch=p.line==cursor_.line?bracketAt(p):0;}
        if(ch && std::strchr("(){}[]",ch)) {
            const char* pairs="(){}[]"; int idx=static_cast<int>(std::strchr(pairs,ch)-pairs); bool forward=idx%2==0; char other=pairs[idx^1];
            int depth=0; Pos q=p; bool found=false;
            for(int steps=0;steps<20000;++steps) {
                int k=bracketAt(q);
                if(k==ch) ++depth; else if(k==other && --depth==0) {found=true; break;}
                Pos n=forward?right(q):left(q); if(n.line==q.line && n.col==q.col) break; q=n;
            }
            auto box=[&](Pos b){float x=cellX(column(b)),y=rowY(b.line); d->AddRect({x,y},{x+cw,y+lh},c.bracket,0,0,std::max(1.0f,scale*0.5f));};
            if(found) {box(p); box(q);}
        }
    }
    // Text with highlighting; block comments carry in from the lines above.
    bool block=false; std::vector<Tok> tok;
    for(int l=0;l<top_ && l<lineCount;++l) classify(lines_[static_cast<size_t>(l)],block,tok);
    for(int l=top_;l<lineCount && l<=top_+rows;++l) {
        char n[12]; std::snprintf(n,sizeof n,"%d",l+1);
        pixelfont::draw(d,{pos.x+gutter-cw*0.8f-static_cast<float>(pixelfont::width(n))*scale,rowY(l)+1.5f*scale},scale,l+1==errorLine?IM_COL32(255,241,232,255):c.gutter,n);
        d->PushClipRect({pos.x+gutter,pos.y},{pos.x+size.x,pos.y+size.y},true);
        drawCodeLine(d,{cellX(0),rowY(l)},scale,lines_[static_cast<size_t>(l)],block,c);
        d->PopClipRect();
    }
    // Cursor.
    if(focused_ && std::fmod(ImGui::GetTime()-blink_,1.0)<0.6) {float x=cellX(column(cursor_)),y=rowY(cursor_.line); d->AddRectFilled({x,y},{x+std::max(1.0f,scale),y+lh},c.cursor);}
    d->PopClipRect();
    drawCompletion(d,origin,cw,lh,scale,c,{pos.x+size.x,pos.y+size.y});
    return changed_;
}
}
