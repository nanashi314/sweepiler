#include "GameLanguage.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
namespace ims {
// ---------- syntax ----------
struct GameExpr {
    enum Kind {Number,Name,Index,Unary,Binary} kind{Number};
    std::string op,name; uint64_t value{};
    std::vector<std::shared_ptr<GameExpr>> args;
    int line{};
};
using ExprPtr=std::shared_ptr<GameExpr>;
struct GameNode {
    enum Kind {Assign,AssignBit,Let,If,For,Pixel,At} kind{Assign};
    std::string name; int width{};
    ExprPtr target,value,second;                       // index / value / extra
    std::vector<std::pair<ExprPtr,std::vector<std::shared_ptr<GameNode>>>> branches; // if/elif; null condition = else
    std::vector<std::shared_ptr<GameNode>> body;       // for
    int line{};
};
using NodePtr=std::shared_ptr<GameNode>;
namespace {
[[noreturn]] void fail(int line,const std::string& why) {throw std::invalid_argument("line "+std::to_string(line)+": "+why);}
std::vector<std::string> tokenize(const std::string& text,int line) {
    std::vector<std::string> t; size_t i=0;
    while(i<text.size()) {
        char c=text[i];
        if(c=='#') break;
        if(std::isspace(static_cast<unsigned char>(c))) {++i; continue;}
        if(std::isalpha(static_cast<unsigned char>(c)) || c=='_') {size_t j=i; while(j<text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) || text[j]=='_')) ++j; t.push_back(text.substr(i,j-i)); i=j; continue;}
        if(std::isdigit(static_cast<unsigned char>(c))) {size_t j=i; while(j<text.size() && std::isalnum(static_cast<unsigned char>(text[j]))) ++j; t.push_back(text.substr(i,j-i)); i=j; continue;}
        static const char* two[]={"==","!=","<=",">=","<<",">>"};
        bool matched=false;
        for(auto op:two) if(text.compare(i,2,op)==0) {t.push_back(op); i+=2; matched=true; break;}
        if(matched) continue;
        if(std::string("()[]=+-&|^~<>").find(c)!=std::string::npos) {t.push_back(std::string(1,c)); ++i; continue;}
        fail(line,std::string("unexpected character '")+c+"'");
    }
    return t;
}
uint64_t number(const std::string& s,int line) {
    try {
        size_t used=0; uint64_t v=0;
        if(s.size()>2 && s[0]=='0' && (s[1]=='x' || s[1]=='X')) v=std::stoull(s.substr(2),&used,16),used+=2;
        else if(s.size()>2 && s[0]=='0' && (s[1]=='b' || s[1]=='B')) v=std::stoull(s.substr(2),&used,2),used+=2;
        else v=std::stoull(s,&used,10);
        if(used!=s.size()) fail(line,"bad number '"+s+"'");
        return v;
    } catch(const std::invalid_argument&) {fail(line,"bad number '"+s+"'");} catch(const std::out_of_range&) {fail(line,"number too large");}
}
bool isName(const std::string& s) {return !s.empty() && (std::isalpha(static_cast<unsigned char>(s[0])) || s[0]=='_');}
const std::vector<std::string> keywords{"display","input","state","tick","screen","end","let","if","elif","else","for","pixel","at","and","or","not","hold","press"};
class ExprParser {
public:
    ExprParser(const std::vector<std::string>& t,size_t start,size_t stop,int line):t_(t),i_(start),stop_(stop),line_(line) {}
    ExprPtr parse() {auto e=logicOr(); if(i_!=stop_) fail(line_,"unexpected '"+t_[i_]+"'"); return e;}
private:
    bool peek(const std::string& s) const {return i_<stop_ && t_[i_]==s;}
    bool take(const std::string& s) {if(peek(s)) {++i_; return true;} return false;}
    ExprPtr make(GameExpr::Kind k,std::string op,std::vector<ExprPtr> args) {auto e=std::make_shared<GameExpr>(); e->kind=k; e->op=std::move(op); e->args=std::move(args); e->line=line_; return e;}
    ExprPtr logicOr() {auto l=logicAnd(); while(take("or")) l=make(GameExpr::Binary,"or",{l,logicAnd()}); return l;}
    ExprPtr logicAnd() {auto l=logicNot(); while(take("and")) l=make(GameExpr::Binary,"and",{l,logicNot()}); return l;}
    ExprPtr logicNot() {if(take("not")) return make(GameExpr::Unary,"not",{logicNot()}); return compare();}
    ExprPtr compare() {
        auto l=bitOr();
        for(const char* op:{"==","!=","<=",">=","<",">"}) if(take(op)) return make(GameExpr::Binary,op,{l,bitOr()});
        return l;
    }
    ExprPtr bitOr() {auto l=bitXor(); while(take("|")) l=make(GameExpr::Binary,"|",{l,bitXor()}); return l;}
    ExprPtr bitXor() {auto l=bitAnd(); while(take("^")) l=make(GameExpr::Binary,"^",{l,bitAnd()}); return l;}
    ExprPtr bitAnd() {auto l=shift(); while(take("&")) l=make(GameExpr::Binary,"&",{l,shift()}); return l;}
    ExprPtr shift() {auto l=sum(); for(;;) {if(take("<<")) l=make(GameExpr::Binary,"<<",{l,sum()}); else if(take(">>")) l=make(GameExpr::Binary,">>",{l,sum()}); else return l;}}
    ExprPtr sum() {auto l=unary(); for(;;) {if(take("+")) l=make(GameExpr::Binary,"+",{l,unary()}); else if(take("-")) l=make(GameExpr::Binary,"-",{l,unary()}); else return l;}}
    ExprPtr unary() {if(take("~")) return make(GameExpr::Unary,"~",{unary()}); if(take("-")) return make(GameExpr::Unary,"-",{unary()}); return primary();}
    ExprPtr primary() {
        if(i_>=stop_) fail(line_,"expression expected");
        if(take("(")) {auto e=logicOr(); if(!take(")")) fail(line_,"')' expected"); return e;}
        const auto& s=t_[i_++];
        if(std::isdigit(static_cast<unsigned char>(s[0]))) {auto e=make(GameExpr::Number,"",{}); e->value=number(s,line_); return e;}
        if(!isName(s) || std::find(keywords.begin(),keywords.end(),s)!=keywords.end()) fail(line_,"unexpected '"+s+"'");
        auto e=make(GameExpr::Name,"",{}); e->name=s;
        if(take("[")) {auto i=logicOr(); if(!take("]")) fail(line_,"']' expected"); auto x=make(GameExpr::Index,"",{i}); x->name=s; return x;}
        return e;
    }
    const std::vector<std::string>& t_; size_t i_,stop_; int line_;
};
ExprPtr parseExpr(const std::vector<std::string>& t,size_t a,size_t b,int line) {if(a>=b) fail(line,"expression expected"); return ExprParser(t,a,b,line).parse();}
}

GameProgram parseGame(const std::string& source) {
    GameProgram p; std::istringstream in(source); std::string text; int line=0;
    std::vector<std::pair<int,std::vector<std::string>>> lines;
    while(std::getline(in,text)) {++line; auto t=tokenize(text,line); if(!t.empty()) lines.push_back({line,std::move(t)});}
    size_t at=0; bool sawTick=false,sawScreen=false;
    std::function<std::vector<NodePtr>(bool,std::vector<std::string>&)> block=[&](bool screen,std::vector<std::string>& stop) {
        std::vector<NodePtr> nodes;
        while(at<lines.size()) {
            auto& [ln,t]=lines[at];
            if(std::find(stop.begin(),stop.end(),t[0])!=stop.end()) return nodes;
            ++at; auto n=std::make_shared<GameNode>(); n->line=ln;
            auto eq=std::find(t.begin(),t.end(),"=")-t.begin();
            if(t[0]=="let") {
                if(t.size()<5 || t[3]!="=" || !isName(t[1])) fail(ln,"let NAME WIDTH = expr");
                n->kind=GameNode::Let; n->name=t[1]; n->width=static_cast<int>(number(t[2],ln)); n->value=parseExpr(t,4,t.size(),ln);
                if(n->width<1 || n->width>64) fail(ln,"width must be 1..64");
            } else if(t[0]=="if") {
                n->kind=GameNode::If; auto cond=parseExpr(t,1,t.size(),ln);
                std::vector<std::string> ends{"elif","else","end"};
                n->branches.push_back({cond,block(screen,ends)});
                for(;;) {
                    if(at>=lines.size()) fail(ln,"'end' expected for if");
                    auto& [l2,t2]=lines[at]; ++at;
                    if(t2[0]=="elif") {auto c=parseExpr(t2,1,t2.size(),l2); n->branches.push_back({c,block(screen,ends)});}
                    else if(t2[0]=="else") {if(t2.size()!=1) fail(l2,"else takes nothing"); std::vector<std::string> e{"end"}; n->branches.push_back({nullptr,block(screen,e)}); if(at>=lines.size()) fail(l2,"'end' expected"); ++at; break;}
                    else {if(t2.size()!=1) fail(l2,"end takes nothing"); break;}
                }
            } else if(t[0]=="for") {
                if(t.size()<4 || !isName(t[1])) fail(ln,"for NAME FIRST LAST");
                // Bounds are single tokens or parenthesised expressions: split at the top level.
                std::vector<size_t> starts; int depth=0;
                for(size_t k=2;k<t.size();++k) {if(depth==0) starts.push_back(k); if(t[k]=="(") ++depth; else if(t[k]==")") --depth;}
                if(starts.size()<2) fail(ln,"for NAME FIRST LAST");
                size_t second=starts.size()==2?starts[1]:starts[1];
                n->kind=GameNode::For; n->name=t[1]; n->target=parseExpr(t,2,second,ln); n->value=parseExpr(t,second,t.size(),ln);
                std::vector<std::string> e{"end"}; n->body=block(screen,e);
                if(at>=lines.size()) fail(ln,"'end' expected for for"); ++at;
            } else if(t[0]=="pixel") {
                if(!screen) fail(ln,"pixel is only allowed in screen");
                if(eq>=static_cast<long>(t.size())) fail(ln,"pixel X Y = expr");
                // X and Y: split the coordinates at the top level as for loop bounds.
                std::vector<size_t> starts; int depth=0;
                for(size_t k=1;k<static_cast<size_t>(eq);++k) {if(depth==0) starts.push_back(k); if(t[k]=="(") ++depth; else if(t[k]==")") --depth;}
                if(starts.size()!=2) fail(ln,"pixel X Y = expr (use parentheses for expressions)");
                n->kind=GameNode::Pixel; n->target=parseExpr(t,starts[0],starts[1],ln); n->second=parseExpr(t,starts[1],static_cast<size_t>(eq),ln); n->value=parseExpr(t,static_cast<size_t>(eq)+1,t.size(),ln);
            } else if(t[0]=="at") {
                std::vector<size_t> starts; int depth=0;
                for(size_t k=1;k<t.size();++k) {if(depth==0) starts.push_back(k); if(t[k]=="(") ++depth; else if(t[k]==")") --depth;}
                if(starts.size()!=2) fail(ln,"at X Y");
                n->kind=GameNode::At; n->target=parseExpr(t,starts[0],starts[1],ln); n->second=parseExpr(t,starts[1],t.size(),ln);
            } else if(isName(t[0]) && eq<static_cast<long>(t.size())) {
                n->name=t[0];
                if(eq==1) n->kind=GameNode::Assign;
                else if(t[1]=="[" && t[static_cast<size_t>(eq)-1]=="]") {n->kind=GameNode::AssignBit; n->target=parseExpr(t,2,static_cast<size_t>(eq)-1,ln);}
                else fail(ln,"NAME = expr or NAME[expr] = expr");
                n->value=parseExpr(t,static_cast<size_t>(eq)+1,t.size(),ln);
            } else fail(ln,"unknown statement '"+t[0]+"'");
            nodes.push_back(n);
        }
        return nodes;
    };
    while(at<lines.size()) {
        auto [ln,t]=lines[at]; ++at;
        if(t[0]=="display") {if(t.size()!=3) fail(ln,"display WIDTH HEIGHT"); p.width=static_cast<int>(number(t[1],ln)); p.height=static_cast<int>(number(t[2],ln));}
        else if(t[0]=="input") {
            if(t.size()<2 || t.size()>3 || !isName(t[1])) fail(ln,"input NAME [hold|press]");
            if(t.size()==3 && t[2]!="hold" && t[2]!="press") fail(ln,"input mode is hold or press");
            p.keys.push_back({t[1],t.size()==3 && t[2]=="press"});
        } else if(t[0]=="state") {
            if((t.size()!=3 && t.size()!=5) || !isName(t[1]) || (t.size()==5 && t[3]!="=")) fail(ln,"state NAME WIDTH [= VALUE]");
            GameProgram::Variable v{t[1],static_cast<int>(number(t[2],ln)),t.size()==5?number(t[4],ln):0};
            if(v.width<1 || v.width>64) fail(ln,"width must be 1..64");
            if(v.width<64 && (v.initial>>v.width)) fail(ln,"initial value does not fit");
            p.state.push_back(v);
        } else if(t[0]=="tick" || t[0]=="screen") {
            bool screen=t[0]=="screen"; if(t.size()!=1) fail(ln,t[0]+" takes nothing");
            if((screen && sawScreen) || (!screen && sawTick)) fail(ln,"only one "+t[0]+" block");
            std::vector<std::string> e{"end"}; auto body=block(screen,e);
            if(at>=lines.size()) fail(ln,"'end' expected"); ++at;
            (screen?p.screen:p.tick)=std::move(body); (screen?sawScreen:sawTick)=true;
        } else fail(ln,"expected display, input, state, tick or screen");
    }
    if(p.width<1 || p.height<1 || p.width>16 || p.height>32) fail(0,"display must be 1..16 x 1..32");
    if(p.keys.empty() || p.keys.size()>6) fail(0,"a program needs 1..6 inputs");
    if(p.state.empty()) fail(0,"a program needs state");
    std::vector<std::string> names; for(auto& k:p.keys) names.push_back(k.name); for(auto& v:p.state) names.push_back(v.name);
    for(size_t i=0;i<names.size();++i) {
        if(std::find(keywords.begin(),keywords.end(),names[i])!=keywords.end()) fail(0,"'"+names[i]+"' is a keyword");
        for(size_t j=i+1;j<names.size();++j) if(names[i]==names[j]) fail(0,"duplicate name '"+names[i]+"'");
    }
    return p;
}

// ---------- shared width rules ----------
namespace {
uint64_t mask(int w) {return w>=64?~0ull:((1ull<<w)-1);}
int bitsOf(uint64_t v) {int w=1; while(w<64 && (v>>w)) ++w; return w;}
}

// ---------- compiler: symbolic execution into NOR gates ----------
namespace {
// Unsized constants (literals, loop variables, constant results) carry an exact
// 64-bit value and take the width of the other operand.
struct Vec {std::vector<NorSignal> bits; bool constant{}; bool sized{true}; uint64_t value{}; int width() const {return static_cast<int>(bits.size());}};
class Compiler {
public:
    Compiler(const GameProgram& p):p_(p) {}
    FrameCircuit run(const std::string& name) {
        for(const auto& v:p_.state) {Vec x; for(int i=0;i<v.width;++i) x.bits.push_back(b_.input(v.name+"["+std::to_string(i)+"]")); env_[v.name]=x;}
        for(const auto& k:p_.keys) {Vec x; x.bits.push_back(b_.input(k.name)); env_[k.name]=x; readOnly_.insert(k.name);}
        exec(p_.tick,false);
        auto after=env_;
        for(int y=0;y<p_.height;++y) for(int x=0;x<p_.width;++x) env_[pixelName(x,y)]=constant(0,1);
        exec(p_.screen,true);
        for(const auto& v:p_.state) {auto x=after[v.name]; for(int i=0;i<v.width;++i) b_.output("next "+v.name+"["+std::to_string(i)+"]",x.bits[static_cast<size_t>(i)]);}
        for(int y=0;y<p_.height;++y) for(int x=0;x<p_.width;++x) b_.output("pixel "+std::to_string(x)+","+std::to_string(y),env_[pixelName(x,y)].bits[0]);
        FrameCircuit c; c.name=name; c.net=b_.finish(); c.hinted=hinted_;
        for(const auto& v:p_.state) c.stateBits+=static_cast<size_t>(v.width);
        for(const auto& k:p_.keys) {c.keys.push_back(k.name); c.keyPress.push_back(k.press);}
        c.displayWidth=p_.width; c.displayHeight=p_.height;
        for(const auto& v:p_.state) for(int i=0;i<v.width;++i) c.initial.push_back(static_cast<uint8_t>((v.initial>>i)&1));
        return c;
    }
private:
    static std::string pixelName(int x,int y) {return "@pixel "+std::to_string(x)+","+std::to_string(y);}
    Vec constant(uint64_t v,int w) {Vec x; x.constant=true; x.value=v&mask(w); for(int i=0;i<w;++i) x.bits.push_back(NorBuilder::constant((v>>i)&1)); return x;}
    Vec unsized(uint64_t v) {Vec x=constant(v,bitsOf(v)); x.value=v; x.sized=false; return x;}
    static int widthOf(const Vec& a,const Vec& b) {
        if(!a.sized && !b.sized) return 64;
        if(!a.sized) return b.width(); if(!b.sized) return a.width();
        return std::max(a.width(),b.width());
    }
    Vec resize(const Vec& a,int w) {
        if(a.constant) {Vec x=constant(a.value,w); return x;}
        Vec x; for(int i=0;i<w;++i) x.bits.push_back(i<a.width()?a.bits[static_cast<size_t>(i)]:NorBuilder::constant(false)); return x;
    }
    uint64_t constValue(const ExprPtr& e) {auto v=eval(e); if(!v.constant) fail(e->line,"a constant expression is required here"); return v.value;}
    NorSignal nonzero(const Vec& a) {return b_.any(a.bits);}
    Vec boolean(NorSignal s) {Vec x; x.bits.push_back(s); if(s.constant()) {x.constant=true; x.value=s.inverted;} return x;}
    Vec add(const Vec& a,const Vec& b,int w,bool subtract) {
        Vec x; NorSignal carry=NorBuilder::constant(subtract);
        for(int i=0;i<w;++i) {
            NorSignal p=a.bits[static_cast<size_t>(i)],q=b.bits[static_cast<size_t>(i)]; if(subtract) q=!q;
            NorSignal s=b_.xorGate(p,q); x.bits.push_back(b_.xorGate(s,carry));
            carry=b_.orGate(b_.andGate(p,q),b_.andGate(s,carry));
        }
        return x;
    }
    // a < b (unsigned) from the borrow of a - b at full width.
    NorSignal less(const Vec& a,const Vec& b) {
        int w=std::max({a.width(),b.width(),a.constant?bitsOf(a.value):1,b.constant?bitsOf(b.value):1}); auto x=resize(a,w),y=resize(b,w);
        NorSignal carry=NorBuilder::constant(true);
        for(int i=0;i<w;++i) {NorSignal p=x.bits[static_cast<size_t>(i)],q=!y.bits[static_cast<size_t>(i)]; NorSignal s=b_.xorGate(p,q); carry=b_.orGate(b_.andGate(p,q),b_.andGate(s,carry));}
        return !carry;
    }
    NorSignal equal(const Vec& a,const Vec& b) {
        int w=std::max({a.width(),b.width(),a.constant?bitsOf(a.value):1,b.constant?bitsOf(b.value):1}); auto x=resize(a,w),y=resize(b,w);
        std::vector<NorSignal> diff; for(int i=0;i<w;++i) diff.push_back(b_.xorGate(x.bits[static_cast<size_t>(i)],y.bits[static_cast<size_t>(i)]));
        return !b_.any(diff);
    }
    Vec eval(const ExprPtr& e) {
        switch(e->kind) {
        case GameExpr::Number: return unsized(e->value);
        case GameExpr::Name: {
            if(auto c=loop_.find(e->name);c!=loop_.end()) return unsized(c->second);
            auto it=env_.find(e->name); if(it==env_.end() || e->name[0]=='@') fail(e->line,"unknown name '"+e->name+"'");
            return it->second;
        }
        case GameExpr::Index: {
            auto it=env_.find(e->name); if(it==env_.end()) fail(e->line,"unknown name '"+e->name+"'");
            const Vec& v=it->second; auto i=eval(e->args[0]);
            if(i.constant) return boolean(i.value<static_cast<uint64_t>(v.width())?v.bits[static_cast<size_t>(i.value)]:NorBuilder::constant(false));
            std::vector<NorSignal> terms; for(int j=0;j<v.width();++j) terms.push_back(b_.andGate(equal(i,constant(static_cast<uint64_t>(j),bitsOf(static_cast<uint64_t>(j)))),v.bits[static_cast<size_t>(j)]));
            return boolean(b_.any(terms));
        }
        case GameExpr::Unary: {
            auto a=eval(e->args[0]);
            if(e->op=="not") return a.constant?constant(a.value==0,1):boolean(!nonzero(a));
            if(e->op=="~") {if(!a.sized) return unsized(~a.value); if(a.constant) return constant(~a.value,a.width()); Vec x; for(auto s:a.bits) x.bits.push_back(!s); return x;}
            if(!a.sized) return unsized(0-a.value);
            if(a.constant) return constant(0-a.value,a.width());
            return add(constant(0,a.width()),a,a.width(),true);
        }
        case GameExpr::Binary: {
            const auto& op=e->op;
            if(op=="and" || op=="or") {
                auto a=eval(e->args[0]),b=eval(e->args[1]);
                if(a.constant && b.constant) return constant(op=="and"?(a.value && b.value):(a.value || b.value),1);
                NorSignal x=nonzero(a),y=nonzero(b); return boolean(op=="and"?b_.andGate(x,y):b_.orGate(x,y));
            }
            auto a=eval(e->args[0]),b=eval(e->args[1]);
            if(op=="<<" || op==">>") {
                if(!b.constant) fail(e->line,"shift amounts must be constant");
                int w=a.width(); uint64_t s=b.value;
                if(!a.sized) return unsized(s>=64?0:op=="<<"?a.value<<s:a.value>>s);
                if(a.constant) return constant(s>=64?0:op=="<<"?a.value<<s:a.value>>s,w);
                Vec x; for(int i=0;i<w;++i) {int64_t from=op=="<<"?static_cast<int64_t>(i)-static_cast<int64_t>(s):static_cast<int64_t>(i)+static_cast<int64_t>(s); x.bits.push_back(from>=0 && from<w?a.bits[static_cast<size_t>(from)]:NorBuilder::constant(false));}
                return x;
            }
            int w=widthOf(a,b);
            if(a.constant && b.constant) {
                uint64_t x=a.value,y=b.value; bool exact=!a.sized && !b.sized;
                auto make=[&](uint64_t v){return exact?unsized(v):constant(v,w);};
                if(op=="+") return make(x+y); if(op=="-") return make(x-y);
                if(op=="&") return make(x&y); if(op=="|") return make(x|y); if(op=="^") return make(x^y);
                if(op=="==") return constant(x==y,1); if(op=="!=") return constant(x!=y,1);
                if(op=="<") return constant(x<y,1); if(op=="<=") return constant(x<=y,1); if(op==">") return constant(x>y,1); if(op==">=") return constant(x>=y,1);
            }
            if(op=="==") return boolean(equal(a,b)); if(op=="!=") return boolean(!equal(a,b));
            if(op=="<") return boolean(less(a,b)); if(op==">") return boolean(less(b,a));
            if(op=="<=") return boolean(!less(b,a)); if(op==">=") return boolean(!less(a,b));
            auto x=resize(a,w),y=resize(b,w);
            if(op=="+") return add(x,y,w,false); if(op=="-") return add(x,y,w,true);
            Vec r; for(int i=0;i<w;++i) {auto p=x.bits[static_cast<size_t>(i)],q=y.bits[static_cast<size_t>(i)]; r.bits.push_back(op=="&"?b_.andGate(p,q):op=="|"?b_.orGate(p,q):b_.xorGate(p,q));}
            return r;
        }
        }
        fail(e->line,"bad expression");
    }
    Vec& target(const std::string& name,int line) {
        if(readOnly_.contains(name)) fail(line,"inputs cannot be assigned");
        if(loop_.contains(name)) fail(line,"loop variables cannot be assigned");
        auto it=env_.find(name); if(it==env_.end() || name[0]=='@') fail(line,"unknown name '"+name+"'");
        return it->second;
    }
    void exec(const std::vector<NodePtr>& nodes,bool screen) {
        for(const auto& n:nodes) switch(n->kind) {
        case GameNode::Assign: {auto v=eval(n->value); auto& t=target(n->name,n->line); t=resize(v,t.width()); break;}
        case GameNode::AssignBit: {
            auto i=eval(n->target); auto v=eval(n->value); NorSignal bit=nonzero(v); auto& t=target(n->name,n->line);
            Vec x=resize(t,t.width());
            for(int j=0;j<x.width();++j) {
                if(i.constant) {if(static_cast<uint64_t>(j)==i.value) x.bits[static_cast<size_t>(j)]=bit; continue;}
                x.bits[static_cast<size_t>(j)]=b_.mux(equal(i,constant(static_cast<uint64_t>(j),bitsOf(static_cast<uint64_t>(j)))),x.bits[static_cast<size_t>(j)],bit);
            }
            t=x; break;
        }
        case GameNode::Let: {
            if(env_.contains(n->name) || loop_.contains(n->name)) fail(n->line,"'"+n->name+"' already exists");
            env_[n->name]=resize(eval(n->value),n->width); lets_.push_back(n->name); break;
        }
        case GameNode::Pixel: {
            int64_t x=static_cast<int64_t>(constValue(n->target)),y=static_cast<int64_t>(constValue(n->second));
            if(x<0 || y<0 || x>=p_.width || y>=p_.height) fail(n->line,"pixel outside the display");
            env_[pixelName(static_cast<int>(x),static_cast<int>(y))]=boolean(nonzero(eval(n->value))); break;
        }
        case GameNode::At: {b_.hint(static_cast<float>(constValue(n->target)),static_cast<float>(constValue(n->second))); hinted_=true; break;}
        case GameNode::For: {
            uint64_t first=constValue(n->target),last=constValue(n->value);
            if(loop_.contains(n->name) || env_.contains(n->name)) fail(n->line,"'"+n->name+"' already exists");
            if(last>=first && last-first>4096) fail(n->line,"loop too long");
            for(uint64_t v=first;v<=last && last>=first;++v) {loop_[n->name]=v; scoped(n->body,screen); if(v==last) break;}
            loop_.erase(n->name); break;
        }
        case GameNode::If: {
            // Every branch runs on a copy; assignments merge through multiplexers.
            auto before=env_; std::vector<std::pair<NorSignal,std::map<std::string,Vec>>> outcomes; NorSignal taken=NorBuilder::constant(false);
            bool sawElse=false;
            for(const auto& [cond,body]:n->branches) {
                env_=before;
                NorSignal c=cond?nonzero(eval(cond)):NorBuilder::constant(true); if(!cond) sawElse=true;
                NorSignal here=b_.andGate(c,!taken); taken=b_.orGate(taken,c);
                scoped(body,screen); outcomes.push_back({here,env_});
            }
            if(!sawElse) outcomes.push_back({!taken,before});
            env_=before;
            for(auto& [name,value]:env_) {
                if(readOnly_.contains(name)) continue;
                Vec merged=outcomes.back().second.at(name);
                for(size_t k=outcomes.size()-1;k-->0;) {
                    const Vec& v=outcomes[k].second.at(name); Vec m;
                    for(int j=0;j<value.width();++j) m.bits.push_back(b_.mux(outcomes[k].first,merged.bits[static_cast<size_t>(j)],v.bits[static_cast<size_t>(j)]));
                    merged=m;
                }
                value=merged;
            }
            break;
        }
        }
    }
    // Lets inside a block end with it.
    void scoped(const std::vector<NodePtr>& body,bool screen) {
        size_t mark=lets_.size(); exec(body,screen);
        while(lets_.size()>mark) {env_.erase(lets_.back()); lets_.pop_back();}
    }
    const GameProgram& p_;
    NorBuilder b_;
    std::map<std::string,Vec> env_;
    std::map<std::string,uint64_t> loop_;
    std::set<std::string> readOnly_;
    std::vector<std::string> lets_;
    bool hinted_{};
};
}
FrameCircuit compileGame(const std::string& source,const std::string& name) {return Compiler(parseGame(source)).run(name);}

// ---------- reference interpreter (integers, independent of the gate lowering) ----------
namespace {
struct Value {uint64_t v{};int w{1};};
class Interpreter {
public:
    Interpreter(const GameProgram& p,std::map<std::string,Value> env,std::set<std::string> readOnly):p_(p),env_(std::move(env)),readOnly_(std::move(readOnly)) {}
    std::map<std::string,Value> env_;
    void exec(const std::vector<NodePtr>& nodes) {
        for(const auto& n:nodes) switch(n->kind) {
        case GameNode::Assign: {auto& t=env_.at(n->name); t.v=eval(n->value).v&mask(t.w); break;}
        case GameNode::AssignBit: {auto i=eval(n->target).v; bool bit=eval(n->value).v!=0; auto& t=env_.at(n->name); if(i<static_cast<uint64_t>(t.w)) t.v=(t.v&~(1ull<<i))|(static_cast<uint64_t>(bit)<<i); break;}
        case GameNode::Let: {env_[n->name]={eval(n->value).v&mask(n->width),n->width}; lets_.push_back(n->name); break;}
        case GameNode::Pixel: {auto x=eval(n->target).v,y=eval(n->second).v; env_[pixel(static_cast<int>(x),static_cast<int>(y))]={eval(n->value).v!=0,1}; break;}
        case GameNode::At: break;
        case GameNode::For: {
            auto first=eval(n->target).v,last=eval(n->value).v;
            for(uint64_t v=first;v<=last && last>=first;++v) {loop_[n->name]=v; scoped(n->body); if(v==last) break;}
            loop_.erase(n->name); break;
        }
        case GameNode::If: for(const auto& [cond,body]:n->branches) if(!cond || eval(cond).v!=0) {scoped(body); break;} break;
        }
    }
    static std::string pixel(int x,int y) {return "@pixel "+std::to_string(x)+","+std::to_string(y);}
private:
    void scoped(const std::vector<NodePtr>& body) {size_t mark=lets_.size(); exec(body); while(lets_.size()>mark) {env_.erase(lets_.back()); lets_.pop_back();}}
    Value eval(const ExprPtr& e) {
        switch(e->kind) {
        case GameExpr::Number: return {e->value,0};
        case GameExpr::Name: {if(auto c=loop_.find(e->name);c!=loop_.end()) return {c->second,0}; return env_.at(e->name);}
        case GameExpr::Index: {auto x=env_.at(e->name); auto i=eval(e->args[0]).v; return {i<static_cast<uint64_t>(x.w)?(x.v>>i)&1:0,1};}
        case GameExpr::Unary: {auto a=eval(e->args[0]); if(e->op=="not") return {a.v==0,1}; int w=a.w?a.w:64; if(e->op=="~") return {~a.v&mask(w),a.w}; return {(0-a.v)&mask(w),a.w};}
        case GameExpr::Binary: {
            auto a=eval(e->args[0]),b=eval(e->args[1]); const auto& op=e->op;
            const int w=!a.w && !b.w?0:!a.w?b.w:!b.w?a.w:std::max(a.w,b.w),m=w?w:64;
            if(op=="and") return {a.v!=0 && b.v!=0,1}; if(op=="or") return {a.v!=0 || b.v!=0,1};
            if(op=="<<") return {b.v>=64?0:(a.v<<b.v)&mask(a.w?a.w:64),a.w}; if(op==">>") return {b.v>=64?0:a.v>>b.v,a.w};
            if(op=="==") return {a.v==b.v,1}; if(op=="!=") return {a.v!=b.v,1};
            if(op=="<") return {a.v<b.v,1}; if(op=="<=") return {a.v<=b.v,1}; if(op==">") return {a.v>b.v,1}; if(op==">=") return {a.v>=b.v,1};
            if(op=="+") return {(a.v+b.v)&mask(m),w}; if(op=="-") return {(a.v-b.v)&mask(m),w};
            if(op=="&") return {a.v&b.v&mask(m),w}; if(op=="|") return {(a.v|b.v)&mask(m),w}; return {(a.v^b.v)&mask(m),w};
        }
        }
        return {};
    }
    const GameProgram& p_;
    std::set<std::string> readOnly_;
    std::map<std::string,uint64_t> loop_;
    std::vector<std::string> lets_;
};
}
GameReference::GameReference(GameProgram program):program_(std::move(program)) {}
std::vector<uint64_t> GameReference::initial() const {std::vector<uint64_t> v; for(auto& s:program_.state) v.push_back(s.initial); return v;}
std::vector<uint64_t> GameReference::step(const std::vector<uint64_t>& state,const std::vector<uint8_t>& keys,std::vector<uint8_t>* display) const {
    std::map<std::string,Value> env; std::set<std::string> ro;
    for(size_t i=0;i<program_.state.size();++i) env[program_.state[i].name]={state[i]&mask(program_.state[i].width),program_.state[i].width};
    for(size_t i=0;i<program_.keys.size();++i) {env[program_.keys[i].name]={keys[i]&1u,1}; ro.insert(program_.keys[i].name);}
    Interpreter run(program_,env,ro); run.exec(program_.tick);
    std::vector<uint64_t> next; for(auto& s:program_.state) next.push_back(run.env_.at(s.name).v);
    if(display) {
        for(int y=0;y<program_.height;++y) for(int x=0;x<program_.width;++x) run.env_[Interpreter::pixel(x,y)]={0,1};
        run.exec(program_.screen);
        display->clear(); for(int y=0;y<program_.height;++y) for(int x=0;x<program_.width;++x) display->push_back(static_cast<uint8_t>(run.env_.at(Interpreter::pixel(x,y)).v));
    }
    return next;
}
std::vector<uint8_t> packGameState(const GameProgram& p,const std::vector<uint64_t>& values) {
    std::vector<uint8_t> bits; for(size_t i=0;i<p.state.size();++i) for(int b=0;b<p.state[i].width;++b) bits.push_back(static_cast<uint8_t>((values[i]>>b)&1)); return bits;
}
std::vector<uint64_t> unpackGameState(const GameProgram& p,const std::vector<uint8_t>& bits) {
    std::vector<uint64_t> v; size_t k=0; for(const auto& s:p.state) {uint64_t x=0; for(int b=0;b<s.width;++b) x|=static_cast<uint64_t>(bits[k++]&1)<<b; v.push_back(x);} return v;
}
namespace {
const char* examples[][2]={
    {"Paint",R"IMSG(# Paint: arrows move the cursor, A toggles the pixel under it.
# "at" keeps each pixel's logic together, which keeps the board small.
display 6 6
input LEFT press
input RIGHT press
input UP press
input DOWN press
input A press
state x 3 = 2
state y 3 = 2
state canvas 36
state blink 1
tick
  for j 0 5
    for i 0 5
      at i j
      if A and x == i and y == j
        canvas[(j << 2) + (j << 1) + i] = not canvas[(j << 2) + (j << 1) + i]
      end
    end
  end
  at 7 0
  if LEFT and x > 0
    x = x - 1
  end
  if RIGHT and x < 5
    x = x + 1
  end
  at 7 2
  if UP and y > 0
    y = y - 1
  end
  if DOWN and y < 5
    y = y + 1
  end
  blink = not blink
end
screen
  for j 0 5
    for i 0 5
      at i j
      let here 1 = x == i and y == j
      pixel i j = canvas[(j << 2) + (j << 1) + i] ^ (blink and here)
    end
  end
end
)IMSG"},
    {"Catch",R"IMSG(# Catch: move the paddle to catch the falling ball. A miss flashes the screen.
display 6 8
input LEFT hold
input RIGHT hold
state paddle 3 = 2
state ballx 3 = 1
state bally 3
state rng 5 = 21
state slow 1
state flash 2
tick
  rng = (rng << 1) | (((rng >> 4) ^ (rng >> 2)) & 1)
  if LEFT and paddle > 0
    paddle = paddle - 1
  elif RIGHT and paddle < 4
    paddle = paddle + 1
  end
  slow = not slow
  if flash > 0
    flash = flash - 1
  elif slow
    if bally < 7
      bally = bally + 1
    else
      if ballx != paddle and ballx != paddle + 1
        flash = 3
      end
      bally = 0
      ballx = rng & 3
      if rng[4]
        ballx = ballx + 2
      end
    end
  end
end
screen
  for i 0 5
    for j 0 7
      pixel i j = flash > 0 or (i == ballx and j == bally) or (j == 7 and (i == paddle or i == paddle + 1))
    end
  end
end
)IMSG"},
    {"Pong",R"IMSG(# Pong: keep the ball in play with the paddle on the left (UP / DOWN).
display 8 6
input UP hold
input DOWN hold
state paddle 3 = 2
state bx 3 = 4
state by 3 = 2
state dx 1
state dy 1 = 1
state slow 1
state lost 2
tick
  if UP and paddle > 0
    paddle = paddle - 1
  elif DOWN and paddle < 4
    paddle = paddle + 1
  end
  slow = not slow
  if lost > 0
    lost = lost - 1
    bx = 4
    by = 2
  elif slow
    if by == 0
      dy = 1
    elif by == 5
      dy = 0
    end
    if bx == 7
      dx = 0
    elif bx == 1 and (by == paddle or by == paddle + 1)
      dx = 1
    end
    if bx == 0
      lost = 3
    else
      if dx
        bx = bx + 1
      else
        bx = bx - 1
      end
      if dy
        by = by + 1
      else
        by = by - 1
      end
    end
  end
end
screen
  for j 0 5
    for i 0 7
      pixel i j = (i == bx and j == by) or (i == 0 and (j == paddle or j == paddle + 1)) or (lost > 0 and j == 0)
    end
  end
end
)IMSG"},
};
}
const char* gameExample(int i) {return i>=0 && i<static_cast<int>(std::size(examples))?examples[i][1]:nullptr;}
const char* gameExampleName(int i) {return i>=0 && i<static_cast<int>(std::size(examples))?examples[i][0]:nullptr;}
}
