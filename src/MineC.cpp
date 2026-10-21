#include "MineC.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <iterator>
#include <optional>
#include <set>
#include <stdexcept>
namespace ims {
// ================= syntax =================
struct MineCExpr {
    enum Kind {Number,Name,Index,Unary,Binary,Ternary,Call,Cast} kind{Number};
    std::string op,name; int64_t value{}; MineCType type;
    std::vector<std::shared_ptr<MineCExpr>> args; int line{};
};
struct MineCStmt {
    enum Kind {Block,Declare,Assign,Call,If,For,Return,Break,Continue} kind{Block};
    std::string name,op; MineCType type; int count{}; bool array{};
    std::shared_ptr<MineCExpr> index,value,condition; std::vector<std::shared_ptr<MineCExpr>> init,args;
    std::vector<std::shared_ptr<MineCStmt>> body; std::shared_ptr<MineCStmt> start,step,otherwise; int line{};
};
using ExprP=std::shared_ptr<MineCExpr>;
using StmtP=std::shared_ptr<MineCStmt>;
namespace {
[[noreturn]] void fail(int line,const std::string& why) {throw std::invalid_argument("line "+std::to_string(line)+": "+why);}
struct Token {enum Kind {Ident,Number,Punct,End} kind{End}; std::string text; int64_t value{}; int line{};};
std::vector<Token> lex(const std::string& s) {
    std::vector<Token> t; size_t i=0; int line=1;
    static const char* three[]={"<<=",">>="};
    static const char* two[]={"==","!=","<=",">=","&&","||","<<",">>","+=","-=","*=","/=","%=","&=","|=","^=","++","--"};
    while(i<s.size()) {
        char c=s[i];
        if(c=='\n') {++line; ++i; continue;}
        if(std::isspace(static_cast<unsigned char>(c))) {++i; continue;}
        if(c=='/' && i+1<s.size() && s[i+1]=='/') {while(i<s.size() && s[i]!='\n') ++i; continue;}
        if(c=='/' && i+1<s.size() && s[i+1]=='*') {i+=2; while(i+1<s.size() && !(s[i]=='*' && s[i+1]=='/')) {if(s[i]=='\n') ++line; ++i;} i+=2; continue;}
        if(std::isalpha(static_cast<unsigned char>(c)) || c=='_') {size_t j=i; while(j<s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j]=='_')) ++j; t.push_back({Token::Ident,s.substr(i,j-i),0,line}); i=j; continue;}
        if(std::isdigit(static_cast<unsigned char>(c))) {
            size_t j=i; while(j<s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j]=='_')) ++j;
            std::string text=s.substr(i,j-i); std::string digits; for(char d:text) if(d!='_') digits+=d;
            uint64_t v=0; size_t used=0;
            try {
                if(digits.size()>2 && digits[0]=='0' && (digits[1]=='x' || digits[1]=='X')) {v=std::stoull(digits.substr(2),&used,16); used+=2;}
                else if(digits.size()>2 && digits[0]=='0' && (digits[1]=='b' || digits[1]=='B')) {v=std::stoull(digits.substr(2),&used,2); used+=2;}
                else v=std::stoull(digits,&used,10);
            } catch(const std::exception&) {fail(line,"bad number '"+text+"'");}
            if(used!=digits.size()) fail(line,"bad number '"+text+"'");
            t.push_back({Token::Number,text,static_cast<int64_t>(v),line}); i=j; continue;
        }
        bool matched=false;
        for(auto op:three) if(s.compare(i,3,op)==0) {t.push_back({Token::Punct,op,0,line}); i+=3; matched=true; break;}
        if(!matched) for(auto op:two) if(s.compare(i,2,op)==0) {t.push_back({Token::Punct,op,0,line}); i+=2; matched=true; break;}
        if(matched) continue;
        if(std::string("+-*/%&|^~!<>=?:;,(){}[]#").find(c)!=std::string::npos) {t.push_back({Token::Punct,std::string(1,c),0,line}); ++i; continue;}
        fail(line,std::string("unexpected character '")+c+"'");
    }
    t.push_back({Token::End,"",0,line});
    return t;
}
// uintN / intN / int / bool
bool typeName(const std::string& s,MineCType& t) {
    if(s=="bool") {t={1,false}; return true;}
    if(s=="int") {t={32,true}; return true;}
    auto sized=[&](const std::string& prefix,bool sign){
        if(s.size()<=prefix.size() || s.compare(0,prefix.size(),prefix)!=0) return false;
        for(size_t i=prefix.size();i<s.size();++i) if(!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
        int w=std::stoi(s.substr(prefix.size())); if(w<1 || w>64) return false; t={w,sign}; return true;
    };
    return sized("uint",false) || sized("int",true);
}
const std::set<std::string> reserved{"if","else","for","while","return","break","continue","void","const","input","hold","press","pixel","at","bool","int"};
class Parser {
public:
    explicit Parser(const std::string& source):t_(lex(source)) {}
    MineCProgram run() {
        while(!at(Token::End)) {
            if(take("#")) {
                auto& d=word("directive");
                if(d!="display") fail(line(),"unknown directive #"+d);
                p_.width=static_cast<int>(constant()); p_.height=static_cast<int>(constant());
                continue;
            }
            if(takeWord("input")) {
                bool press=false; if(takeWord("press")) press=true; else takeWord("hold");
                std::string n=newName(); expect(";"); p_.keys.push_back({n,press}); continue;
            }
            if(takeWord("const")) {
                MineCType ty; if(!typeName(word("type"),ty)) fail(line(),"type expected");
                std::string n=newName(); expect("="); int64_t v=constant(); expect(";"); p_.constants[n]=v; continue;
            }
            int ln=line(); bool isVoid=takeWord("void"); MineCType ty;
            if(!isVoid && !typeName(word("type"),ty)) fail(ln,"type expected");
            std::string n=newName();
            if(take("(")) {function(n,!isVoid,ty,ln); continue;}
            if(isVoid) fail(ln,"void is only for functions");
            MineCProgram::Global g{n,ty,0,{},ln};
            if(take("[")) {int64_t c=constant(); if(c<1 || c>4096) fail(ln,"array size must be 1..4096"); g.count=static_cast<int>(c); expect("]");}
            if(take("=")) {
                if(g.count) {expect("{"); if(!peek("}")) do {g.init.push_back(constant());} while(take(",")); expect("}"); if(static_cast<int>(g.init.size())>g.count) fail(ln,"too many initial values");}
                else g.init.push_back(constant());
            }
            expect(";"); p_.globals.push_back(g);
        }
        if(p_.width<1 || p_.height<1 || p_.width>32 || p_.height>32) fail(0,"#display must be 1..32 x 1..32");
        if(p_.keys.empty() || p_.keys.size()>6) fail(0,"a program needs 1..6 inputs");
        if(p_.globals.empty()) fail(0,"a program needs global state");
        if(!p_.functions.contains("tick") || p_.functions.at("tick").returns || !p_.functions.at("tick").params.empty()) fail(0,"void tick() is required");
        if(p_.functions.contains("draw") && (p_.functions.at("draw").returns || !p_.functions.at("draw").params.empty())) fail(0,"draw must be void draw()");
        return std::move(p_);
    }
private:
    // ---- tokens
    const Token& cur() const {return t_[i_];}
    int line() const {return cur().line;}
    bool at(Token::Kind k) const {return cur().kind==k;}
    bool peek(const std::string& s,size_t ahead=0) const {return i_+ahead<t_.size() && t_[i_+ahead].kind!=Token::End && t_[i_+ahead].text==s && t_[i_+ahead].kind!=Token::Number;}
    bool take(const std::string& s) {if(peek(s) && cur().kind==Token::Punct) {++i_; return true;} return false;}
    bool takeWord(const std::string& s) {if(cur().kind==Token::Ident && cur().text==s) {++i_; return true;} return false;}
    // A missing ';' belongs to the line of the token before it, not the next one.
    void expect(const std::string& s) {if(!take(s)) fail(s==";" && i_>0?t_[i_-1].line:line(),"'"+s+"' expected"+(at(Token::End)?"":" before '"+cur().text+"'"));}
    const std::string& word(const char* what) {if(cur().kind!=Token::Ident) fail(line(),std::string(what)+" expected"); return t_[i_++].text;}
    std::string newName() {
        auto n=word("name"); MineCType ty;
        if(reserved.contains(n) || typeName(n,ty)) fail(line(),"'"+n+"' is reserved");
        return n;
    }
    bool typeAhead() const {MineCType ty; return cur().kind==Token::Ident && typeName(cur().text,ty);}
    // ---- compile-time constants (used for sizes, initial values and #display)
    int64_t constant() {auto e=expr(); return fold(e);}
    int64_t fold(const ExprP& e) {
        switch(e->kind) {
        case MineCExpr::Number: return e->value;
        case MineCExpr::Name: {auto it=p_.constants.find(e->name); if(it==p_.constants.end()) fail(e->line,"'"+e->name+"' is not a constant"); return it->second;}
        case MineCExpr::Unary: {int64_t a=fold(e->args[0]); return e->op=="-"?static_cast<int64_t>(0-static_cast<uint64_t>(a)):e->op=="~"?~a:e->op=="!"?!a:a;}
        case MineCExpr::Binary: {
            int64_t a=fold(e->args[0]),b=fold(e->args[1]); const auto& o=e->op; uint64_t ua=static_cast<uint64_t>(a),ub=static_cast<uint64_t>(b);
            if(o=="+") return static_cast<int64_t>(ua+ub); if(o=="-") return static_cast<int64_t>(ua-ub); if(o=="*") return static_cast<int64_t>(ua*ub);
            if(o=="/" || o=="%") {if(!b) fail(e->line,"constant division by zero"); return o=="/"?a/b:a%b;}
            if(o=="&") return a&b; if(o=="|") return a|b; if(o=="^") return a^b;
            if(o=="<<") return b<0 || b>63?0:static_cast<int64_t>(ua<<b); if(o==">>") return b<0 || b>63?(a<0?-1:0):a>>b;
            if(o=="==") return a==b; if(o=="!=") return a!=b; if(o=="<") return a<b; if(o=="<=") return a<=b; if(o==">") return a>b; if(o==">=") return a>=b;
            if(o=="&&") return a && b; if(o=="||") return a || b;
            break;
        }
        case MineCExpr::Ternary: return fold(e->args[0])?fold(e->args[1]):fold(e->args[2]);
        default: break;
        }
        fail(e->line,"constant expression expected");
    }
    // ---- declarations
    void function(const std::string& n,bool returns,MineCType ty,int ln) {
        if(p_.functions.contains(n)) fail(ln,"duplicate function '"+n+"'");
        MineCProgram::Function f{n,returns,ty,{},nullptr,ln};
        if(!take(")")) {
            do {MineCType pt; if(!typeName(word("parameter type"),pt)) fail(line(),"parameter type expected"); f.params.push_back({newName(),pt});} while(take(","));
            expect(")");
        }
        f.body=block(); p_.functions[n]=f;
    }
    StmtP make(MineCStmt::Kind k,int ln) {auto s=std::make_shared<MineCStmt>(); s->kind=k; s->line=ln; return s;}
    StmtP block() {
        int ln=line(); expect("{"); auto b=make(MineCStmt::Block,ln);
        while(!take("}")) {if(at(Token::End)) fail(ln,"'}' expected"); b->body.push_back(statement());}
        return b;
    }
    StmtP declaration(bool needSemicolon) {
        int ln=line(); MineCType ty; typeName(word("type"),ty);
        auto s=make(MineCStmt::Declare,ln); s->type=ty; s->name=newName();
        if(take("[")) {int64_t c=constant(); if(c<1 || c>4096) fail(ln,"array size must be 1..4096"); s->count=static_cast<int>(c); s->array=true; expect("]");}
        if(take("=")) {
            if(s->array) {expect("{"); if(!peek("}")) do {s->init.push_back(expr());} while(take(",")); expect("}"); if(static_cast<int>(s->init.size())>s->count) fail(ln,"too many initial values");}
            else s->value=expr();
        }
        if(needSemicolon) expect(";");
        return s;
    }
    // Assignment, ++/--, or a call, without the semicolon (also used in for).
    StmtP simple() {
        int ln=line();
        if(take("++") || take("--")) {
            std::string op=t_[i_-1].text; auto s=make(MineCStmt::Assign,ln); s->name=word("name");
            if(take("[")) {s->index=expr(); expect("]");}
            s->op=op=="++"?"+=":"-="; s->value=number(1,ln); return s;
        }
        std::string n=word("statement");
        if(take("(")) {auto s=make(MineCStmt::Call,ln); s->name=n; if(!take(")")) {do {s->args.push_back(expr());} while(take(",")); expect(")");} return s;}
        auto s=make(MineCStmt::Assign,ln); s->name=n;
        if(take("[")) {s->index=expr(); expect("]");}
        if(take("++")) {s->op="+="; s->value=number(1,ln); return s;}
        if(take("--")) {s->op="-="; s->value=number(1,ln); return s;}
        for(const char* op:{"=","+=","-=","*=","/=","%=","&=","|=","^=","<<=",">>="}) if(take(op)) {s->op=op; s->value=expr(); return s;}
        fail(ln,"assignment expected after '"+n+"'");
    }
    StmtP statement() {
        int ln=line();
        if(peek("{")) return block();
        if(typeAhead()) return declaration(true);
        if(takeWord("if")) {
            auto s=make(MineCStmt::If,ln); expect("("); s->condition=expr(); expect(")");
            s->body.push_back(statement()); if(takeWord("else")) s->otherwise=statement(); return s;
        }
        if(takeWord("for")) {
            auto s=make(MineCStmt::For,ln); expect("(");
            if(!peek(";")) s->start=typeAhead()?declaration(false):simple();
            expect(";"); if(!peek(";")) s->condition=expr(); expect(";");
            if(!peek(")")) s->step=simple();
            expect(")"); s->body.push_back(statement()); return s;
        }
        if(takeWord("while")) fail(ln,"while loops are not supported: use for with constant bounds");
        if(takeWord("return")) {auto s=make(MineCStmt::Return,ln); if(!peek(";")) s->value=expr(); expect(";"); return s;}
        if(takeWord("break")) {expect(";"); return make(MineCStmt::Break,ln);}
        if(takeWord("continue")) {expect(";"); return make(MineCStmt::Continue,ln);}
        auto s=simple(); expect(";"); return s;
    }
    // ---- expressions (C precedence; no assignment or comma)
    ExprP node(MineCExpr::Kind k,const std::string& op,std::vector<ExprP> args,int ln) {auto e=std::make_shared<MineCExpr>(); e->kind=k; e->op=op; e->args=std::move(args); e->line=ln; return e;}
    ExprP number(int64_t v,int ln) {auto e=node(MineCExpr::Number,"",{},ln); e->value=v; return e;}
    ExprP expr() {
        auto c=binary(0); int ln=line();
        if(take("?")) {auto a=expr(); expect(":"); auto b=expr(); return node(MineCExpr::Ternary,"?",{c,a,b},ln);}
        return c;
    }
    ExprP binary(int level) {
        static const std::vector<std::vector<std::string>> levels{{"||"},{"&&"},{"|"},{"^"},{"&"},{"==","!="},{"<","<=",">",">="},{"<<",">>"},{"+","-"},{"*","/","%"}};
        if(level==static_cast<int>(levels.size())) return unary();
        auto l=binary(level+1);
        for(;;) {
            bool found=false; int ln=line();
            for(const auto& op:levels[static_cast<size_t>(level)]) if(cur().kind==Token::Punct && cur().text==op) {++i_; l=node(MineCExpr::Binary,op,{l,binary(level+1)},ln); found=true; break;}
            if(!found) return l;
        }
    }
    ExprP unary() {
        int ln=line();
        for(const char* op:{"-","!","~","+"}) if(cur().kind==Token::Punct && cur().text==op) {++i_; auto a=unary(); return op[0]=='+'?a:node(MineCExpr::Unary,op,{a},ln);}
        if(peek("(") && t_[i_+1].kind==Token::Ident) {MineCType ty; if(typeName(t_[i_+1].text,ty) && i_+2<t_.size() && t_[i_+2].text==")") {i_+=3; auto e=node(MineCExpr::Cast,"",{unary()},ln); e->type=ty; return e;}}
        return primary();
    }
    ExprP primary() {
        int ln=line();
        if(take("(")) {auto e=expr(); expect(")"); return e;}
        if(at(Token::Number)) return number(t_[i_++].value,ln);
        if(takeWord("true")) return number(1,ln);
        if(takeWord("false")) return number(0,ln);
        std::string n=word("expression");
        if(take("(")) {auto e=node(MineCExpr::Call,"",{},ln); e->name=n; if(!take(")")) {do {e->args.push_back(expr());} while(take(",")); expect(")");} return e;}
        if(take("[")) {auto e=node(MineCExpr::Index,"",{expr()},ln); e->name=n; expect("]"); return e;}
        auto e=node(MineCExpr::Name,"",{},ln); e->name=n; return e;
    }
    std::vector<Token> t_; size_t i_{}; MineCProgram p_;
};
uint64_t maskOf(int w) {return w>=64?~0ull:((1ull<<w)-1);}
int bitsFor(int64_t v) {int n=1; while(n<63 && (v>>n)) ++n; return n;}
// The value of v in type t: wrapped to t's width, sign-extended if signed.
int64_t normalize(int64_t v,MineCType t) {
    uint64_t u=static_cast<uint64_t>(v)&maskOf(t.width);
    if(t.sign && t.width<64 && (u>>(t.width-1))&1) u|=~maskOf(t.width);
    return static_cast<int64_t>(u);
}
}
MineCProgram parseMineC(const std::string& source) {return Parser(source).run();}

// ================= compiler: predicated symbolic execution into NOR gates =================
namespace {
// Drops gates no output depends on (values computed and then never used), keeping order and hints.
NorNetlist prune(NorNetlist net) {
    const size_t inputs=net.inputs.size(),gates=net.gates.size();
    std::vector<uint8_t> live(gates,0);
    auto mark=[&](NorSignal s){if(!s.constant() && static_cast<size_t>(s.id)>=inputs) live[static_cast<size_t>(s.id)-inputs]=1;};
    for(const auto& [n,s]:net.outputs) mark(s);
    for(size_t g=gates;g-->0;) if(live[g]) for(auto s:net.gates[g]) mark(s);
    std::vector<int> id(gates,-1); NorNetlist out; out.inputs=net.inputs;
    auto map=[&](NorSignal s){if(!s.constant() && static_cast<size_t>(s.id)>=inputs) s.id=id[static_cast<size_t>(s.id)-inputs]; return s;};
    for(size_t g=0;g<gates;++g) if(live[g]) {
        id[g]=out.gateId(out.gates.size());
        out.gates.push_back({map(net.gates[g][0]),map(net.gates[g][1])}); out.hints.push_back(net.hints[g]);
    }
    for(const auto& [n,s]:net.outputs) out.outputs.push_back({n,map(s)});
    return out;
}
}
namespace {
struct Val {bool literal{}; int64_t v{}; MineCType t; std::vector<NorSignal> bits;};
// born: the path condition where a local was declared. Writes made under that
// same condition need no multiplexer: when it is false, nothing that reads the
// local has any effect. Globals are born under "always".
struct Var {MineCType t; int count{}; std::vector<std::vector<NorSignal>> elems; bool readOnly{}; bool constant{}; int64_t value{}; NorSignal born{-1,true};};
class CCompiler {
public:
    explicit CCompiler(const MineCProgram& p):p_(p) {}
    FrameCircuit run(const std::string& name) {
        scopes_.emplace_back();
        for(const auto& g:p_.globals) {
            Var v; v.t=g.type; v.count=g.count;
            int n=std::max(1,g.count);
            for(int e=0;e<n;++e) {std::vector<NorSignal> bits; for(int i=0;i<g.type.width;++i) bits.push_back(b_.input(g.name+(g.count?"["+std::to_string(e)+"]":"")+"."+std::to_string(i))); v.elems.push_back(bits);}
            declare(g.name,v,g.line);
        }
        for(const auto& k:p_.keys) {Var v; v.t={1,false}; v.readOnly=true; v.elems.push_back({b_.input(k.name)}); declare(k.name,v,0);}
        for(const auto& [n,c]:p_.constants) {Var v; v.constant=true; v.value=c; declare(n,v,0);}
        live_=NorBuilder::constant(true);
        call("tick",{},0);
        std::vector<std::vector<std::vector<NorSignal>>> state;
        for(const auto& g:p_.globals) state.push_back(scopes_[0].at(g.name).elems);
        pixels_.assign(static_cast<size_t>(p_.width*p_.height),NorBuilder::constant(false));
        if(p_.functions.contains("draw")) call("draw",{},0);
        for(size_t gi=0;gi<p_.globals.size();++gi) for(size_t e=0;e<state[gi].size();++e) for(size_t i=0;i<state[gi][e].size();++i)
            b_.output("next "+p_.globals[gi].name+"."+std::to_string(e)+"."+std::to_string(i),state[gi][e][i]);
        for(int y=0;y<p_.height;++y) for(int x=0;x<p_.width;++x) b_.output("pixel "+std::to_string(x)+","+std::to_string(y),pixels_[static_cast<size_t>(x+p_.width*y)]);
        FrameCircuit c; c.name=name; c.net=prune(b_.finish()); c.hinted=hinted_;
        for(const auto& g:p_.globals) c.stateBits+=static_cast<size_t>(g.type.width*std::max(1,g.count));
        for(const auto& k:p_.keys) {c.keys.push_back(k.name); c.keyPress.push_back(k.press);}
        c.displayWidth=p_.width; c.displayHeight=p_.height;
        for(const auto& g:p_.globals) for(int e=0;e<std::max(1,g.count);++e) {
            int64_t v=normalize(e<static_cast<int>(g.init.size())?g.init[static_cast<size_t>(e)]:0,g.type);
            for(int i=0;i<g.type.width;++i) c.initial.push_back(static_cast<uint8_t>((static_cast<uint64_t>(v)>>i)&1));
        }
        return c;
    }
private:
    // ---- values
    std::vector<NorSignal> constBits(int64_t v,int w) {std::vector<NorSignal> r; for(int i=0;i<w;++i) r.push_back(NorBuilder::constant(i<64 && ((static_cast<uint64_t>(v)>>i)&1)));return r;}
    Val literal(int64_t v) {Val x; x.literal=true; x.v=v; return x;}
    Val typed(std::vector<NorSignal> bits,MineCType t) {Val x; x.t=t; x.bits=std::move(bits); return x;}
    Val boolean(NorSignal s) {return typed({s},{1,false});}
    // Bits of x in type t (literals: their value; typed: extended by their own sign).
    std::vector<NorSignal> convert(const Val& x,MineCType t) {
        if(x.literal) return constBits(normalize(x.v,t),t.width);
        std::vector<NorSignal> r;
        for(int i=0;i<t.width;++i) r.push_back(i<x.t.width?x.bits[static_cast<size_t>(i)]:x.t.sign?x.bits.back():NorBuilder::constant(false));
        return r;
    }
    static MineCType common(const Val& a,const Val& b) {
        if(a.literal) return b.t; if(b.literal) return a.t;
        return {std::max(a.t.width,b.t.width),a.t.sign || b.t.sign};
    }
    NorSignal nonzero(const Val& x) {return x.literal?NorBuilder::constant(x.v!=0):b_.any(x.bits);}
    std::vector<NorSignal> add(const std::vector<NorSignal>& a,const std::vector<NorSignal>& b,bool subtract,NorSignal* carryOut=nullptr) {
        std::vector<NorSignal> r; NorSignal carry=NorBuilder::constant(subtract);
        for(size_t i=0;i<a.size();++i) {
            NorSignal p=a[i],q=subtract?!b[i]:b[i],s=b_.xorGate(p,q);
            r.push_back(b_.xorGate(s,carry)); carry=b_.orGate(b_.andGate(p,q),b_.andGate(s,carry));
        }
        if(carryOut) *carryOut=carry;
        return r;
    }
    std::vector<NorSignal> negate(const std::vector<NorSignal>& a) {return add(constBits(0,static_cast<int>(a.size())),a,true);}
    NorSignal lessThan(const std::vector<NorSignal>& a,const std::vector<NorSignal>& b,bool sign) {
        auto x=a,y=b; if(sign) {x.back()=!x.back(); y.back()=!y.back();}
        NorSignal carry; add(x,y,true,&carry); return !carry; // borrow of x-y
    }
    NorSignal equal(const std::vector<NorSignal>& a,const std::vector<NorSignal>& b) {
        std::vector<NorSignal> d; for(size_t i=0;i<a.size();++i) d.push_back(b_.xorGate(a[i],b[i])); return !b_.any(d);
    }
    std::vector<NorSignal> select(NorSignal s,const std::vector<NorSignal>& whenZero,const std::vector<NorSignal>& whenOne) {
        std::vector<NorSignal> r; for(size_t i=0;i<whenZero.size();++i) r.push_back(b_.mux(s,whenZero[i],whenOne[i])); return r;
    }
    // The sum of weighted bits (column c has weight 2^c) truncated to the
    // number of columns: full and half adders reduce each column to one bit
    // and pass carries on, so every extra operand costs one adder per column.
    std::vector<NorSignal> compress(std::vector<std::vector<NorSignal>> cols) {
        std::vector<NorSignal> out;
        for(size_t c=0;c<cols.size();++c) {
            std::vector<NorSignal> live; int ones=0;
            for(auto s:cols[c]) if(s.constant()) ones+=s.inverted; else live.push_back(s);
            if(c+1<cols.size()) for(int k=0;k<ones/2;++k) cols[c+1].push_back(NorBuilder::constant(true));
            if(ones%2) live.push_back(NorBuilder::constant(true));
            for(size_t i=0;live.size()-i>=2;) {
                NorSignal p=live[i],q=live[i+1],s=b_.xorGate(p,q);
                if(live.size()-i>=3) {
                    NorSignal r=live[i+2];
                    live.push_back(b_.xorGate(s,r)); if(c+1<cols.size()) cols[c+1].push_back(b_.orGate(b_.andGate(p,q),b_.andGate(s,r))); i+=3;
                } else {
                    live.push_back(s); if(c+1<cols.size()) cols[c+1].push_back(b_.andGate(p,q)); i+=2;
                }
            }
            out.push_back(live.empty()?NorBuilder::constant(false):live.back());
        }
        return out;
    }
    std::vector<NorSignal> multiply(const std::vector<NorSignal>& a,const std::vector<NorSignal>& b) {
        size_t w=a.size(); std::vector<std::vector<NorSignal>> cols(w);
        if(a==b) {
            // Squares: a_i a_j + a_j a_i = 2 a_i a_j, and a_i a_i = a_i.
            for(size_t i=0;i<w;++i) {
                if(2*i<w) cols[2*i].push_back(a[i]);
                for(size_t j=i+1;i+j+1<w;++j) cols[i+j+1].push_back(b_.andGate(a[i],a[j]));
            }
        } else for(size_t i=0;i<w;++i) for(size_t j=0;i+j<w;++j) cols[i+j].push_back(b_.andGate(a[j],b[i]));
        return compress(cols);
    }
    // Restoring division; by zero gives all ones and the dividend as remainder.
    void divide(const std::vector<NorSignal>& a,const std::vector<NorSignal>& b,std::vector<NorSignal>& q,std::vector<NorSignal>& r) {
        size_t w=a.size(); auto rem=constBits(0,static_cast<int>(w)+1); auto den=b; den.push_back(NorBuilder::constant(false));
        q.assign(w,NorBuilder::constant(false));
        for(size_t k=w;k-->0;) {
            std::vector<NorSignal> shifted{a[k]}; for(size_t i=0;i<w;++i) shifted.push_back(rem[i]);
            NorSignal carry; auto diff=add(shifted,den,true,&carry);
            q[k]=carry; rem=select(carry,shifted,diff);
        }
        r.assign(rem.begin(),rem.begin()+static_cast<long>(w));
    }
    std::vector<NorSignal> shift(const std::vector<NorSignal>& v,const Val& amount,bool left,bool arithmetic) {
        int w=static_cast<int>(v.size()); NorSignal fill=arithmetic?v.back():NorBuilder::constant(false);
        if(amount.literal) {
            std::vector<NorSignal> r;
            for(int i=0;i<w;++i) {int64_t from=left?i-amount.v:i+amount.v; r.push_back(amount.v<0 || amount.v>=w?(left?NorBuilder::constant(false):fill):from>=0 && from<w?v[static_cast<size_t>(from)]:left?NorBuilder::constant(false):fill);}
            return r;
        }
        auto r=v; int64_t step=1;
        for(size_t k=0;k<amount.bits.size() && step<w;++k,step*=2) {
            std::vector<NorSignal> moved;
            for(int i=0;i<w;++i) {int64_t from=left?i-step:i+step; moved.push_back(from>=0 && from<w?r[static_cast<size_t>(from)]:left?NorBuilder::constant(false):fill);}
            r=select(amount.bits[k],r,moved);
        }
        // Amounts of the width or more (or negative) clear the value or fill it with the sign.
        int cw=std::max(amount.t.width,bitsFor(w))+1;
        NorSignal big=!lessThan(convert(amount,{cw,amount.t.sign}),constBits(w,cw),amount.t.sign);
        if(amount.t.sign) big=b_.orGate(big,amount.bits.back());
        std::vector<NorSignal> out; for(int i=0;i<w;++i) out.push_back(b_.mux(big,r[static_cast<size_t>(i)],left?NorBuilder::constant(false):fill));
        return out;
    }
    // ---- names
    void declare(const std::string& n,Var v,int line) {
        if(scopes_.back().contains(n)) fail(line,"'"+n+"' is already declared");
        scopes_.back()[n]=std::move(v);
    }
    Var* find(const std::string& n) {for(auto it=scopes_.rbegin();it!=scopes_.rend();++it) if(auto f=it->find(n);f!=it->end()) return &f->second; return nullptr;}
    Var& variable(const std::string& n,int line) {auto v=find(n); if(!v) fail(line,"unknown name '"+n+"'"); return *v;}
    // ---- predicates: assignments apply only where the current path is active.
    NorSignal active() {
        NorSignal a=live_;
        if(!functions_.empty()) a=b_.andGate(a,!functions_.back().returned);
        if(!loops_.empty()) a=b_.andGate(a,!b_.orGate(loops_.back().broken,loops_.back().continued));
        return a;
    }
    void store(std::vector<NorSignal>& target,const std::vector<NorSignal>& value,NorSignal born=NorBuilder::constant(true)) {
        NorSignal a=active(); if(a.constant() && !a.inverted) return;
        target=a==born?value:select(a,target,value);
    }
    // ---- expressions
    Val eval(const ExprP& e) {
        switch(e->kind) {
        case MineCExpr::Number: return literal(e->value);
        case MineCExpr::Name: {
            auto& v=variable(e->name,e->line); if(v.constant) return literal(v.value);
            if(v.count) fail(e->line,"array '"+e->name+"' needs an index");
            return typed(v.elems[0],v.t);
        }
        case MineCExpr::Index: {
            Val i=eval(e->args[0]);
            auto& v=variable(e->name,e->line); if(!v.count) fail(e->line,"'"+e->name+"' is not an array");
            if(i.literal) return i.v>=0 && i.v<v.count?typed(v.elems[static_cast<size_t>(i.v)],v.t):typed(constBits(0,v.t.width),v.t);
            std::vector<NorSignal> r=constBits(0,v.t.width);
            for(int k=0;k<v.count;++k) {NorSignal hit=match(i,k); for(int b=0;b<v.t.width;++b) r[static_cast<size_t>(b)]=b_.orGate(r[static_cast<size_t>(b)],b_.andGate(hit,v.elems[static_cast<size_t>(k)][static_cast<size_t>(b)]));}
            return typed(r,v.t);
        }
        case MineCExpr::Cast: {Val a=eval(e->args[0]); if(a.literal) return typed(constBits(normalize(a.v,e->type),e->type.width),e->type); return typed(convert(a,e->type),e->type);}
        case MineCExpr::Unary: {
            Val a=eval(e->args[0]);
            if(e->op=="!") return a.literal?literal(a.v==0):boolean(!nonzero(a));
            if(a.literal) return literal(e->op=="-"?static_cast<int64_t>(0-static_cast<uint64_t>(a.v)):~a.v);
            if(e->op=="~") {std::vector<NorSignal> r; for(auto s:a.bits) r.push_back(!s); return typed(r,a.t);}
            return typed(negate(a.bits),a.t);
        }
        case MineCExpr::Ternary: {
            Val c=eval(e->args[0]),x=eval(e->args[1]),y=eval(e->args[2]);
            if(c.literal) return c.v?x:y;
            if(x.literal && y.literal) {MineCType t{32,true}; return typed(select(nonzero(c),constBits(y.v,32),constBits(x.v,32)),t);}
            MineCType t=common(x,y); return typed(select(nonzero(c),convert(y,t),convert(x,t)),t);
        }
        case MineCExpr::Call: return call(e->name,e->args,e->line);
        case MineCExpr::Binary: return binary(e);
        }
        fail(e->line,"bad expression");
    }
    // Index i equals k (a negative signed index matches nothing).
    NorSignal match(const Val& i,int k) {
        if(i.literal) return NorBuilder::constant(i.v==k);
        // Narrow indices share one decoder: 2^n terms built from two half decoders.
        if(i.t.width<=16) {
            int usable=i.t.sign?i.t.width-1:i.t.width;
            if(k<0 || k>=(1<<usable)) return NorBuilder::constant(false);
            return term(i.bits,static_cast<size_t>(k));
        }
        int w=std::max(i.t.width,bitsFor(k)+1);
        auto bits=convert(i,{w,i.t.sign}); NorSignal hit=equal(bits,constBits(k,w));
        if(i.t.sign) hit=b_.andGate(hit,!i.bits.back());
        return hit;
    }
    // Term k of a one-hot decoder over bits, built on demand from the terms of
    // its two halves, so comparisons of one value with many constants share gates.
    NorSignal term(const std::vector<NorSignal>& bits,size_t k) {
        if(bits.size()==1) return k?bits[0]:!bits[0];
        std::vector<std::pair<int,bool>> key; for(auto s:bits) key.push_back({s.id,s.inverted});
        key.push_back({static_cast<int>(k),false});
        if(auto it=terms_.find(key);it!=terms_.end()) return it->second;
        size_t half=bits.size()/2;
        NorSignal low=term({bits.begin(),bits.begin()+static_cast<long>(half)},k&((size_t{1}<<half)-1));
        NorSignal high=term({bits.begin()+static_cast<long>(half),bits.end()},k>>half);
        return terms_[key]=b_.andGate(low,high);
    }
    Val binary(const ExprP& e) {
        if(e->op=="+" || e->op=="-") {
            // A chain of three or more terms that all wrap at one type is one multi-operand sum.
            std::vector<std::pair<Val,bool>> terms; terms_of(e,false,terms);
            if(terms.size()>=3) {
                Val cur=terms[0].first; bool same=true; std::optional<MineCType> t;
                for(size_t k=1;k<terms.size();++k) {
                    const Val& x=terms[k].first;
                    if(cur.literal && x.literal) {cur=literal(terms[k].second?static_cast<int64_t>(static_cast<uint64_t>(cur.v)-static_cast<uint64_t>(x.v)):static_cast<int64_t>(static_cast<uint64_t>(cur.v)+static_cast<uint64_t>(x.v))); continue;}
                    MineCType step=common(cur,x);
                    if(t && (t->width!=step.width || t->sign!=step.sign)) same=false;
                    t=step; cur=typed({},step);
                }
                if(!t) return cur;
                if(same) {
                    std::vector<std::vector<NorSignal>> cols(static_cast<size_t>(t->width));
                    for(const auto& [x,negative]:terms) {
                        auto bits=convert(x,*t);
                        for(size_t i=0;i<bits.size();++i) cols[i].push_back(negative?!bits[i]:bits[i]);
                        if(negative) cols[0].push_back(NorBuilder::constant(true));
                    }
                    return typed(compress(cols),*t);
                }
                Val acc=terms[0].first;
                for(size_t k=1;k<terms.size();++k) acc=binaryOn(terms[k].second?"-":"+",acc,terms[k].first,e->line);
                return acc;
            }
            return binaryOn(e->op,terms[0].first,terms[1].first,e->line);
        }
        Val a=eval(e->args[0]); Val b=eval(e->args[1]); return binaryOn(e->op,a,b,e->line);
    }
    // Leaves of a left-leaning + / - chain, evaluated left to right.
    void terms_of(const ExprP& e,bool negative,std::vector<std::pair<Val,bool>>& out) {
        if(e->kind==MineCExpr::Binary && (e->op=="+" || e->op=="-") && out.empty()) {
            terms_of(e->args[0],false,out); out.push_back({eval(e->args[1]),e->op=="-"}); return;
        }
        out.push_back({eval(e),negative});
    }
    Val binaryOn(const std::string& o,Val a,Val b,int line) {
        if(o=="&&" || o=="||") {
            if(a.literal && b.literal) return literal(o=="&&"?(a.v && b.v):(a.v || b.v));
            NorSignal x=nonzero(a),y=nonzero(b); return boolean(o=="&&"?b_.andGate(x,y):b_.orGate(x,y));
        }
        if(o=="<<" || o==">>") {
            if(a.literal && b.literal) {int64_t s=b.v; if(o=="<<") return literal(s<0 || s>63?0:static_cast<int64_t>(static_cast<uint64_t>(a.v)<<s)); return literal(s<0 || s>63?(a.v<0?-1:0):a.v>>s);}
            if(a.literal) a=typed(constBits(a.v,64),{64,true});
            return typed(shift(a.bits,b,o=="<<",o==">>" && a.t.sign),a.t);
        }
        if(a.literal && b.literal) {
            uint64_t ua=static_cast<uint64_t>(a.v),ub=static_cast<uint64_t>(b.v);
            if(o=="+") return literal(static_cast<int64_t>(ua+ub)); if(o=="-") return literal(static_cast<int64_t>(ua-ub)); if(o=="*") return literal(static_cast<int64_t>(ua*ub));
            if(o=="/" || o=="%") {if(!b.v) fail(line,"constant division by zero"); return literal(o=="/"?a.v/b.v:a.v%b.v);}
            if(o=="&") return literal(a.v&b.v); if(o=="|") return literal(a.v|b.v); if(o=="^") return literal(a.v^b.v);
            if(o=="==") return literal(a.v==b.v); if(o=="!=") return literal(a.v!=b.v); if(o=="<") return literal(a.v<b.v);
            if(o=="<=") return literal(a.v<=b.v); if(o==">") return literal(a.v>b.v); if(o==">=") return literal(a.v>=b.v);
        }
        if((o=="==" || o=="!=") && a.literal!=b.literal && (a.literal?b:a).t.width<=16) {
            const Val& x=a.literal?b:a; int64_t v=normalize(a.literal?a.v:b.v,x.t);
            NorSignal hit=term(x.bits,static_cast<size_t>(static_cast<uint64_t>(v)&maskOf(x.t.width)));
            return boolean(o=="=="?hit:!hit);
        }
        MineCType t=common(a,b); auto x=convert(a,t),y=convert(b,t);
        if(o=="==") return boolean(equal(x,y)); if(o=="!=") return boolean(!equal(x,y));
        if(o=="<") return boolean(lessThan(x,y,t.sign)); if(o==">") return boolean(lessThan(y,x,t.sign));
        if(o=="<=") return boolean(!lessThan(y,x,t.sign)); if(o==">=") return boolean(!lessThan(x,y,t.sign));
        if(o=="+") return typed(add(x,y,false),t); if(o=="-") return typed(add(x,y,true),t);
        if(o=="*") return typed(multiply(x,y),t);
        if(o=="/" || o=="%") {
            std::vector<NorSignal> q,r;
            if(!t.sign) {divide(x,y,q,r); return typed(o=="/"?q:r,t);}
            NorSignal na=x.back(),nb=y.back();
            divide(select(na,x,negate(x)),select(nb,y,negate(y)),q,r);
            if(o=="/") return typed(select(b_.xorGate(na,nb),q,negate(q)),t);
            return typed(select(na,r,negate(r)),t);
        }
        std::vector<NorSignal> r;
        for(size_t i=0;i<x.size();++i) r.push_back(o=="&"?b_.andGate(x[i],y[i]):o=="|"?b_.orGate(x[i],y[i]):b_.xorGate(x[i],y[i]));
        return typed(r,t);
    }
    // ---- calls (inlined)
    Val call(const std::string& n,const std::vector<ExprP>& args,int line) {
        if(n=="pixel") {
            if(args.size()!=3) fail(line,"pixel(x, y, value)");
            Val x=eval(args[0]),y=eval(args[1]); NorSignal v=nonzero(eval(args[2]));
            if(x.literal && y.literal) {
                if(x.v<0 || y.v<0 || x.v>=p_.width || y.v>=p_.height) fail(line,"pixel outside the display");
                std::vector<NorSignal> cell{pixels_[static_cast<size_t>(x.v+p_.width*y.v)]}; store(cell,{v}); pixels_[static_cast<size_t>(x.v+p_.width*y.v)]=cell[0];
            } else for(int j=0;j<p_.height;++j) for(int i=0;i<p_.width;++i) {
                NorSignal here=b_.andGate(x.literal?NorBuilder::constant(x.v==i):match(x,i),y.literal?NorBuilder::constant(y.v==j):match(y,j));
                auto& cell=pixels_[static_cast<size_t>(i+p_.width*j)]; cell=b_.mux(b_.andGate(active(),here),cell,v);
            }
            return literal(0);
        }
        if(n=="at") {
            if(args.size()!=2) fail(line,"at(x, y)");
            Val x=eval(args[0]),y=eval(args[1]); if(!x.literal || !y.literal) fail(line,"at() needs constants");
            b_.hint(static_cast<float>(x.v),static_cast<float>(y.v)); hinted_=true; return literal(0);
        }
        auto it=p_.functions.find(n); if(it==p_.functions.end()) fail(line,"unknown function '"+n+"'");
        const auto& f=it->second;
        if(args.size()!=f.params.size()) fail(line,"'"+n+"' takes "+std::to_string(f.params.size())+" arguments");
        if(std::find(calling_.begin(),calling_.end(),n)!=calling_.end()) fail(line,"recursion is not supported ('"+n+"')");
        if(calling_.size()>64) fail(line,"calls nested too deeply");
        std::vector<Val> values; for(const auto& a:args) values.push_back(eval(a));
        std::vector<std::map<std::string,Var>> saved; saved.swap(scopes_); scopes_.push_back(saved.front()); scopes_.emplace_back();
        // Globals stay shared: the callee works on the same global scope.
        NorSignal outerLive=live_; NorSignal entry=active();   // the caller's return/break state carries in
        for(size_t i=0;i<f.params.size();++i) {Var v; v.t=f.params[i].second; v.elems.push_back(convert(values[i],v.t)); v.born=entry; declare(f.params[i].first,v,line);}
        live_=entry;
        calling_.push_back(n); functions_.push_back({NorBuilder::constant(false),constBits(0,f.returns?f.type.width:1),entry});
        auto loops=std::move(loops_); loops_.clear();
        exec(f.body);
        loops_=std::move(loops); auto frame=functions_.back(); functions_.pop_back(); calling_.pop_back();
        saved.front()=scopes_.front(); scopes_.swap(saved); live_=outerLive;
        if(f.returns) return typed(frame.value,f.type);
        return literal(0);
    }
    // ---- statements
    void exec(const StmtP& s) {
        switch(s->kind) {
        case MineCStmt::Block: {scopes_.emplace_back(); for(const auto& x:s->body) exec(x); scopes_.pop_back(); break;}
        case MineCStmt::Declare: {
            Var v; v.t=s->type; v.count=s->array?s->count:0;
            for(int e=0;e<std::max(1,v.count);++e) {
                if(!s->array && s->value) v.elems.push_back(convert(eval(s->value),v.t));
                else if(s->array && e<static_cast<int>(s->init.size())) v.elems.push_back(convert(eval(s->init[static_cast<size_t>(e)]),v.t));
                else v.elems.push_back(constBits(0,v.t.width));
            }
            v.born=active(); declare(s->name,v,s->line); break;
        }
        case MineCStmt::Assign: assign(s); break;
        case MineCStmt::Call: call(s->name,s->args,s->line); break;
        case MineCStmt::If: {
            NorSignal c=nonzero(eval(s->condition)); NorSignal saved=live_;
            live_=b_.andGate(saved,c); exec(s->body[0]);
            if(s->otherwise) {live_=b_.andGate(saved,!c); exec(s->otherwise);}
            live_=saved; break;
        }
        case MineCStmt::For: forLoop(s); break;
        case MineCStmt::Return: {
            if(functions_.empty()) fail(s->line,"return outside a function");
            auto& f=functions_.back(); NorSignal a=active();
            const auto& def=p_.functions.at(calling_.back());
            if(def.returns) {if(!s->value) fail(s->line,"return needs a value"); auto v=convert(eval(s->value),def.type);
                // The first return reached on the function's own path just sets the value.
                f.value=f.returned.constant() && !f.returned.inverted && a==f.entry?v:select(a,f.value,v);}
            else if(s->value) fail(s->line,"void function returns a value");
            f.returned=b_.orGate(f.returned,a); break;
        }
        case MineCStmt::Break: if(loops_.empty()) fail(s->line,"break outside a loop"); loops_.back().broken=b_.orGate(loops_.back().broken,active()); break;
        case MineCStmt::Continue: if(loops_.empty()) fail(s->line,"continue outside a loop"); loops_.back().continued=b_.orGate(loops_.back().continued,active()); break;
        }
    }
    void assign(const StmtP& s) {
        // The index, then the right-hand side, each evaluated once; then the target.
        Val i; if(s->index) i=eval(s->index);
        Val value=eval(s->value);
        auto& v=variable(s->name,s->line);
        if(v.readOnly) fail(s->line,"inputs cannot be assigned");
        if(v.constant) fail(s->line,"'"+s->name+"' is constant");
        auto update=[&](size_t e){
            Val x=value;
            if(s->op!="=") x=binaryOn(s->op.substr(0,s->op.size()-1),typed(v.elems[e],v.t),value,s->line);
            store(v.elems[e],convert(x,v.t),v.born);
        };
        if(!s->index) {if(v.count) fail(s->line,"array '"+s->name+"' needs an index"); update(0); return;}
        if(!v.count) fail(s->line,"'"+s->name+"' is not an array");
        if(i.literal) {if(i.v>=0 && i.v<v.count) update(static_cast<size_t>(i.v)); return;}
        NorSignal saved=live_;
        for(int k=0;k<v.count;++k) {live_=b_.andGate(saved,match(i,k)); update(static_cast<size_t>(k));}
        live_=saved;
    }
    void forLoop(const StmtP& s) {
        scopes_.emplace_back();
        std::string counter;
        if(s->start) {
            if(s->start->kind!=MineCStmt::Declare || s->start->array || !s->start->value) fail(s->line,"for needs 'TYPE i = constant'");
            Val v=eval(s->start->value); if(!v.literal) fail(s->line,"loop start must be constant");
            Var c; c.constant=true; c.value=normalize(v.v,s->start->type); declare(s->start->name,c,s->line); counter=s->start->name;
        }
        NorSignal outerLive=live_; live_=active();
        loops_.push_back({NorBuilder::constant(false),NorBuilder::constant(false)});
        for(int n=0;;++n) {
            if(n>10000) fail(s->line,"loop runs more than 10000 times");
            if(s->condition) {Val c=eval(s->condition); if(!c.literal) fail(s->line,"loop condition must be constant (bounds known at compile time)"); if(!c.v) break;}
            loops_.back().continued=NorBuilder::constant(false);
            exec(s->body[0]);
            NorSignal br=loops_.back().broken; if(br.constant() && br.inverted) break;
            if(s->step) {
                if(s->step->kind!=MineCStmt::Assign || s->step->name!=counter || s->step->index) fail(s->line,"for step must update the loop counter");
                Val by=eval(s->step->value); if(!by.literal) fail(s->line,"loop step must be constant");
                auto& c=variable(counter,s->line); MineCType t=s->start->type; int64_t x=c.value;
                const auto& op=s->step->op;
                if(op=="+=") x=static_cast<int64_t>(static_cast<uint64_t>(x)+static_cast<uint64_t>(by.v)); else if(op=="-=") x=static_cast<int64_t>(static_cast<uint64_t>(x)-static_cast<uint64_t>(by.v));
                else if(op=="*=") x*=by.v; else if(op=="<<=") x<<=by.v; else if(op==">>=") x>>=by.v; else if(op=="=") x=by.v;
                else fail(s->line,"unsupported loop step");
                c.value=normalize(x,t);
            }
        }
        loops_.pop_back(); scopes_.pop_back(); live_=outerLive;
    }
    struct FunctionFrame {NorSignal returned; std::vector<NorSignal> value; NorSignal entry;};
    struct LoopFrame {NorSignal broken,continued;};
    const MineCProgram& p_;
    NorBuilder b_;
    std::vector<std::map<std::string,Var>> scopes_;
    std::vector<FunctionFrame> functions_;
    std::vector<LoopFrame> loops_;
    std::vector<std::string> calling_;
    std::vector<NorSignal> pixels_;
    NorSignal live_;
    std::map<std::vector<std::pair<int,bool>>,NorSignal> terms_;
    bool hinted_{};
};
}
FrameCircuit compileMineC(const std::string& source,const std::string& name) {return CCompiler(parseMineC(source)).run(name);}

// ================= interpreter (64-bit integers, real control flow) =================
namespace {
struct IVal {bool literal{}; int64_t v{}; MineCType t;};
struct IVar {MineCType t; int count{}; std::vector<int64_t> elems; bool readOnly{}; bool constant{};};
struct Flow {enum Kind {Normal,Return,Break,Continue} kind{Normal}; IVal value;};
class Interpreter {
public:
    Interpreter(const MineCProgram& p):p_(p) {}
    std::vector<std::map<std::string,IVar>> scopes_;
    std::vector<uint8_t> pixels_;
    void callEntry(const std::string& n) {call(n,{},0);}
private:
    static MineCType common(const IVal& a,const IVal& b) {if(a.literal) return b.t; if(b.literal) return a.t; return {std::max(a.t.width,b.t.width),a.t.sign || b.t.sign};}
    IVar* find(const std::string& n) {for(auto it=scopes_.rbegin();it!=scopes_.rend();++it) if(auto f=it->find(n);f!=it->end()) return &f->second; return nullptr;}
    IVal eval(const ExprP& e) {
        switch(e->kind) {
        case MineCExpr::Number: return {true,e->value,{}};
        case MineCExpr::Name: {auto& v=*find(e->name); if(v.constant) return {true,v.elems[0],{}}; return {false,v.elems[0],v.t};}
        case MineCExpr::Index: {IVal i=eval(e->args[0]); auto& v=*find(e->name); return {false,i.v>=0 && i.v<v.count?v.elems[static_cast<size_t>(i.v)]:0,v.t};}
        case MineCExpr::Cast: {IVal a=eval(e->args[0]); return {false,normalize(a.v,e->type),e->type};}
        case MineCExpr::Unary: {
            IVal a=eval(e->args[0]);
            if(e->op=="!") return a.literal?IVal{true,a.v==0,{}}:IVal{false,a.v==0,{1,false}};
            int64_t r=e->op=="-"?static_cast<int64_t>(0-static_cast<uint64_t>(a.v)):~a.v;
            return a.literal?IVal{true,r,{}}:IVal{false,normalize(r,a.t),a.t};
        }
        case MineCExpr::Ternary: {
            IVal c=eval(e->args[0]),x=eval(e->args[1]),y=eval(e->args[2]);
            if(c.literal) return c.v?x:y;
            if(x.literal && y.literal) {MineCType t{32,true}; return {false,normalize(c.v?x.v:y.v,t),t};}
            MineCType t=common(x,y); return {false,normalize(c.v?x.v:y.v,t),t};
        }
        case MineCExpr::Call: return call(e->name,e->args,e->line);
        case MineCExpr::Binary: return binary(e->op,eval(e->args[0]),eval(e->args[1]));
        }
        return {};
    }
public:
    IVal binary(const std::string& o,IVal a,IVal b) {
        if(o=="&&" || o=="||") {bool r=o=="&&"?(a.v && b.v):(a.v || b.v); return a.literal && b.literal?IVal{true,r,{}}:IVal{false,r,{1,false}};}
        if(o=="<<" || o==">>") {
            int64_t s=b.v;
            if(a.literal && b.literal) {if(o=="<<") return {true,s<0 || s>63?0:static_cast<int64_t>(static_cast<uint64_t>(a.v)<<s),{}}; return {true,s<0 || s>63?(a.v<0?-1:0):a.v>>s,{}};}
            MineCType t=a.literal?MineCType{64,true}:a.t; int64_t v=normalize(a.v,t);
            bool big=s<0 || s>=t.width;
            if(o=="<<") return {false,big?0:normalize(static_cast<int64_t>(static_cast<uint64_t>(v)<<s),t),t};
            if(t.sign) return {false,big?(v<0?-1:0):v>>s,t};
            return {false,big?0:static_cast<int64_t>((static_cast<uint64_t>(v)&maskOf(t.width))>>s),t};
        }
        if(a.literal && b.literal) {
            uint64_t ua=static_cast<uint64_t>(a.v),ub=static_cast<uint64_t>(b.v);
            if(o=="+") return {true,static_cast<int64_t>(ua+ub),{}}; if(o=="-") return {true,static_cast<int64_t>(ua-ub),{}}; if(o=="*") return {true,static_cast<int64_t>(ua*ub),{}};
            if(o=="/") return {true,a.v/b.v,{}}; if(o=="%") return {true,a.v%b.v,{}};
            if(o=="&") return {true,a.v&b.v,{}}; if(o=="|") return {true,a.v|b.v,{}}; if(o=="^") return {true,a.v^b.v,{}};
            if(o=="==") return {true,a.v==b.v,{}}; if(o=="!=") return {true,a.v!=b.v,{}}; if(o=="<") return {true,a.v<b.v,{}};
            if(o=="<=") return {true,a.v<=b.v,{}}; if(o==">") return {true,a.v>b.v,{}}; return {true,a.v>=b.v,{}};
        }
        MineCType t=common(a,b); int64_t x=normalize(a.v,t),y=normalize(b.v,t); uint64_t m=maskOf(t.width);
        uint64_t ux=static_cast<uint64_t>(x)&m,uy=static_cast<uint64_t>(y)&m;
        auto cmp=[&](bool r){return IVal{false,r,{1,false}};};
        if(o=="==") return cmp(x==y); if(o=="!=") return cmp(x!=y);
        if(o=="<") return cmp(t.sign?x<y:ux<uy); if(o==">") return cmp(t.sign?x>y:ux>uy);
        if(o=="<=") return cmp(t.sign?x<=y:ux<=uy); if(o==">=") return cmp(t.sign?x>=y:ux>=uy);
        auto out=[&](uint64_t r){return IVal{false,normalize(static_cast<int64_t>(r),t),t};};
        if(o=="+") return out(ux+uy); if(o=="-") return out(ux-uy); if(o=="*") return out(ux*uy);
        if(o=="/" || o=="%") {
            if(!t.sign) {uint64_t q=uy?ux/uy:m,r=uy?ux%uy:ux; return out(o=="/"?q:r);}
            bool na=x<0,nb=y<0; uint64_t ma=(na?0-ux:ux)&m,mb=(nb?0-uy:uy)&m;
            uint64_t q=mb?ma/mb:m,r=mb?ma%mb:ma;
            if(o=="/") return out(na!=nb?0-q:q);
            return out(na?0-r:r);
        }
        if(o=="&") return out(ux&uy); if(o=="|") return out(ux|uy); return out(ux^uy);
    }
private:
    IVal call(const std::string& n,const std::vector<ExprP>& args,int) {
        if(n=="pixel") {
            IVal x=eval(args[0]),y=eval(args[1]),v=eval(args[2]);
            if(x.v>=0 && y.v>=0 && x.v<p_.width && y.v<p_.height) pixels_[static_cast<size_t>(x.v+p_.width*y.v)]=v.v!=0;
            return {true,0,{}};
        }
        if(n=="at") return {true,0,{}};
        const auto& f=p_.functions.at(n);
        std::vector<IVal> values; for(const auto& a:args) values.push_back(eval(a));
        std::vector<std::map<std::string,IVar>> saved; saved.swap(scopes_); scopes_.push_back(saved.front()); scopes_.emplace_back();
        for(size_t i=0;i<f.params.size();++i) {IVar v; v.t=f.params[i].second; v.elems.push_back(normalize(values[i].v,v.t)); scopes_.back()[f.params[i].first]=v;}
        Flow flow=exec(f.body);
        saved.front()=scopes_.front(); scopes_.swap(saved);
        if(f.returns) return {false,flow.kind==Flow::Return?normalize(flow.value.v,f.type):0,f.type};
        return {true,0,{}};
    }
    Flow exec(const StmtP& s) {
        switch(s->kind) {
        case MineCStmt::Block: {scopes_.emplace_back(); for(const auto& x:s->body) {Flow f=exec(x); if(f.kind!=Flow::Normal) {scopes_.pop_back(); return f;}} scopes_.pop_back(); return {};}
        case MineCStmt::Declare: {
            IVar v; v.t=s->type; v.count=s->array?s->count:0;
            for(int e=0;e<std::max(1,v.count);++e) {
                if(!s->array && s->value) v.elems.push_back(normalize(eval(s->value).v,v.t));
                else if(s->array && e<static_cast<int>(s->init.size())) v.elems.push_back(normalize(eval(s->init[static_cast<size_t>(e)]).v,v.t));
                else v.elems.push_back(0);
            }
            scopes_.back()[s->name]=v; return {};
        }
        case MineCStmt::Assign: {
            IVal i; if(s->index) i=eval(s->index);
            IVal value=eval(s->value);
            auto& v=*find(s->name); int64_t* target=nullptr;
            if(s->index) {if(i.v<0 || i.v>=v.count) return {}; target=&v.elems[static_cast<size_t>(i.v)];}
            else target=&v.elems[0];
            if(s->op!="=") value=binary(s->op.substr(0,s->op.size()-1),IVal{false,*target,v.t},value);
            *target=normalize(value.v,v.t); return {};
        }
        case MineCStmt::Call: call(s->name,s->args,s->line); return {};
        case MineCStmt::If: {
            if(eval(s->condition).v) return exec(s->body[0]);
            if(s->otherwise) return exec(s->otherwise);
            return {};
        }
        case MineCStmt::For: {
            scopes_.emplace_back(); std::string counter;
            if(s->start) {IVar c; c.constant=true; c.elems.push_back(normalize(eval(s->start->value).v,s->start->type)); scopes_.back()[s->start->name]=c; counter=s->start->name;}
            Flow result;
            for(;;) {
                if(s->condition && !eval(s->condition).v) break;
                Flow f=exec(s->body[0]);
                if(f.kind==Flow::Return) {result=f; break;}
                if(f.kind==Flow::Break) break;
                if(s->step) {
                    auto& c=scopes_.back()[counter]; int64_t by=eval(s->step->value).v,x=c.elems[0]; const auto& op=s->step->op;
                    if(op=="+=") x=static_cast<int64_t>(static_cast<uint64_t>(x)+static_cast<uint64_t>(by)); else if(op=="-=") x=static_cast<int64_t>(static_cast<uint64_t>(x)-static_cast<uint64_t>(by));
                    else if(op=="*=") x*=by; else if(op=="<<=") x<<=by; else if(op==">>=") x>>=by; else if(op=="=") x=by;
                    c.elems[0]=normalize(x,s->start->type);
                }
            }
            scopes_.pop_back(); return result;
        }
        case MineCStmt::Return: {Flow f; f.kind=Flow::Return; if(s->value) f.value=eval(s->value); return f;}
        case MineCStmt::Break: return {Flow::Break,{}};
        case MineCStmt::Continue: return {Flow::Continue,{}};
        }
        return {};
    }
    const MineCProgram& p_;
};
}
MineCReference::MineCReference(MineCProgram program):program_(std::move(program)) {}
std::vector<int64_t> MineCReference::initial() const {
    std::vector<int64_t> v;
    for(const auto& g:program_.globals) for(int e=0;e<std::max(1,g.count);++e) v.push_back(normalize(e<static_cast<int>(g.init.size())?g.init[static_cast<size_t>(e)]:0,g.type));
    return v;
}
std::vector<int64_t> MineCReference::step(const std::vector<int64_t>& state,const std::vector<uint8_t>& keys,std::vector<uint8_t>* display) const {
    Interpreter run(program_); run.scopes_.emplace_back(); size_t k=0;
    for(const auto& g:program_.globals) {IVar v; v.t=g.type; v.count=g.count; for(int e=0;e<std::max(1,g.count);++e) v.elems.push_back(normalize(state[k++],g.type)); run.scopes_[0][g.name]=v;}
    for(size_t i=0;i<program_.keys.size();++i) {IVar v; v.t={1,false}; v.readOnly=true; v.elems.push_back(keys[i]&1); run.scopes_[0][program_.keys[i].name]=v;}
    for(const auto& [n,c]:program_.constants) {IVar v; v.constant=true; v.elems.push_back(c); run.scopes_[0][n]=v;}
    run.callEntry("tick");
    std::vector<int64_t> next; for(const auto& g:program_.globals) for(auto e:run.scopes_[0].at(g.name).elems) next.push_back(e);
    if(display) {
        run.pixels_.assign(static_cast<size_t>(program_.width*program_.height),0);
        if(program_.functions.contains("draw")) run.callEntry("draw");
        *display=run.pixels_;
    }
    return next;
}
std::vector<uint8_t> MineCReference::pack(const std::vector<int64_t>& state) const {
    std::vector<uint8_t> bits; size_t k=0;
    for(const auto& g:program_.globals) for(int e=0;e<std::max(1,g.count);++e) {uint64_t v=static_cast<uint64_t>(state[k++]); for(int i=0;i<g.type.width;++i) bits.push_back(static_cast<uint8_t>((v>>i)&1));}
    return bits;
}
std::vector<int64_t> MineCReference::unpack(const std::vector<uint8_t>& bits) const {
    std::vector<int64_t> v; size_t k=0;
    for(const auto& g:program_.globals) for(int e=0;e<std::max(1,g.count);++e) {uint64_t x=0; for(int i=0;i<g.type.width;++i) x|=static_cast<uint64_t>(bits[k++]&1)<<i; v.push_back(normalize(static_cast<int64_t>(x),g.type));}
    return v;
}
}
// ================= examples =================
namespace ims {
namespace {
const char* cExamples[][2]={
    {"Blink",R"MINEC(// Blink: the smallest kind of program. A light turns on and off by itself.
// Press "speed" to make it blink slower or faster.
#display 4 4
input press speed;

bool on = false;     // global variables keep their value from tick to tick
bool slow = false;
uint2 wait = 0;      // ticks to wait before the next change

// tick runs once per tick and changes the state.
void tick() {
    if (speed) slow = !slow;
    if (wait > 0) {
        wait--;
        return;
    }
    on = !on;
    wait = slow ? 3 : 0;
}

// draw makes the picture from the state.
void draw() {
    pixel(1, 1, on);
    pixel(2, 1, on);
    pixel(1, 2, on);
    pixel(2, 2, on);
}
)MINEC"},
    {"Dot",R"MINEC(// Dot: move a dot with the arrow keys. It stops at the edges.
#display 8 8
input hold left;     // "hold": true for every tick the key is down
input hold right;
input hold up;
input hold down;

uint3 x = 3;         // 3 bits: 0..7, one value per column
uint3 y = 3;

void tick() {
    if (left && x > 0) x--;
    if (right && x < 7) x++;
    if (up && y > 0) y--;
    if (down && y < 7) y++;
}

void draw() {
    pixel(x, y, 1);
}
)MINEC"},
    {"Binary",R"MINEC(// Binary: an 8-bit number counting up, shown as 8 lights.
// The rightmost light is bit 0. Press "pause" to stop and start it.
#display 8 3
input press pause;   // "press": true for one tick each time the key goes down

uint8 n = 0;         // 0..255, then it wraps back to 0
bool paused = false;

void tick() {
    if (pause) paused = !paused;
    if (!paused) n++;
}

void draw() {
    // A for loop with fixed bounds; i is a constant in each copy of the body.
    for (int i = 0; i < 8; i++)
        pixel(7 - i, 1, n >> i & 1);
}
)MINEC"},
    {"Bounce",R"MINEC(// Bounce: a ball moves by itself and bounces off the walls.
// Press "pause" to stop and start it.
#display 8 6
input press pause;

uint3 x = 1;
uint3 y = 2;
bool goRight = true;   // the direction is part of the state too
bool goDown = true;
bool paused = false;

void tick() {
    if (pause) paused = !paused;
    if (paused) return;
    // Turn around at the walls, then take one step.
    if (x == 7) goRight = false;
    if (x == 0) goRight = true;
    if (y == 5) goDown = false;
    if (y == 0) goDown = true;
    if (goRight) x++; else x--;
    if (goDown) y++; else y--;
}

void draw() {
    pixel(x, y, 1);
}
)MINEC"},
    {"Smiley",R"MINEC(// Smiley: a picture drawn from a function that returns one row at a time.
// Bit x of a row is the pixel in column x. Press "wink" to wink.
#display 8 8
input press wink;

bool winking = false;

// Constants only: this costs almost no gates, and no state.
uint8 row(int y) {
    if (y == 0) return 0x3C;   // ..####..
    if (y == 1) return 0x42;   // .#....#.
    if (y == 2) return 0xA5;   // #.#..#.#  the eyes
    if (y == 3) return 0x81;   // #......#
    if (y == 4) return 0xA5;   // #.#..#.#  the corners of the mouth
    if (y == 5) return 0x99;   // #..##..#
    if (y == 6) return 0x42;   // .#....#.
    return 0x3C;               // ..####..
}

void tick() {
    if (wink) winking = !winking;
}

void draw() {
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            bool on = (row(y) >> x & 1) != 0;
            if (y == 2 && x == 5 && winking) on = false;   // close one eye
            pixel(x, y, on);
        }
}
)MINEC"},
    {"Dice",R"MINEC(// Dice: press roll for a number from 1 to 6, shown as pips.
#display 7 7
input press roll;

uint3 face = 1;
uint8 seed = 0x2D;

void tick() {
    // A shift register with feedback (an LFSR) changes every tick, so the
    // moment you press decides the number.
    seed = seed << 1 | (seed >> 7 ^ seed >> 5 ^ seed >> 4 ^ seed >> 3) & 1;
    if (roll) {
        uint3 r = (uint3)seed;       // 0..7
        if (r == 0) r = 1;           // fold 0 and 7 into the dice (1 and 6
        if (r == 7) r = 6;           // come up a little more often)
        face = r;
    }
}

void draw() {
    pixel(3, 3, (face & 1) != 0);                     // the middle pip: odd numbers
    pixel(1, 1, face >= 2); pixel(5, 5, face >= 2);   // two corners from 2 up
    pixel(5, 1, face >= 4); pixel(1, 5, face >= 4);   // the other two from 4 up
    pixel(1, 3, face == 6); pixel(5, 3, face == 6);   // the middle row for 6
}
)MINEC"},
    {"Tetris",R"MINEC(// Tetris in a 6 x 10 well. Left / right move, rotate turns, down drops faster.
#display 6 10
input hold left;
input hold right;
input press rotate;
input hold down;

const int W = 6;
const int H = 10;
const int WALLS = 0x3C0F;   // a row padded with 4 wall cells on each side

uint6 rows[10];     // the well, row 0 at the top; bit x is column x
uint3 kind = 2;     // 0..6: I O T S Z J L
uint2 turn = 0;
int5 px = 1;        // the piece's 4 x 4 box: left column ...
int5 py = 0;        // ... and top row
uint2 fall = 0;     // the piece falls one row every fourth tick
uint8 seed = 0x5D;

// The piece as a 4 x 4 mask, bit dy * 4 + dx.
uint16 shape(uint3 k, uint2 r) {
    if (k == 0) return r == 0 ? 0x00F0 : r == 1 ? 0x4444 : r == 2 ? 0x0F00 : 0x2222;
    if (k == 1) return 0x0066;
    if (k == 2) return r == 0 ? 0x0072 : r == 1 ? 0x0262 : r == 2 ? 0x0270 : 0x0232;
    if (k == 3) return r == 0 ? 0x0036 : r == 1 ? 0x0462 : r == 2 ? 0x0360 : 0x0231;
    if (k == 4) return r == 0 ? 0x0063 : r == 1 ? 0x0264 : r == 2 ? 0x0630 : 0x0132;
    if (k == 5) return r == 0 ? 0x0071 : r == 1 ? 0x0226 : r == 2 ? 0x0470 : 0x0322;
    return r == 0 ? 0x0074 : r == 1 ? 0x0622 : r == 2 ? 0x0170 : 0x0223;
}

// Row dy of the mask at column x of a padded row. OR-ing one term per
// possible column is cheaper in gates than shifting by a variable amount.
uint14 placed(uint16 m, int dy, int5 x) {
    uint14 bits = (uint14)(m >> (dy * 4) & 15);
    uint14 out = 0;
    for (int k = -3; k <= 6; k++)
        out |= x == k ? bits << (k + 4) : 0;
    return out;
}

// Whether the piece fits with its box at (x, y). Well rows are picked by
// comparing y with constants, which shares one decoder for all of them.
bool fits(uint16 m, int5 x, int5 y) {
    for (int dy = 0; dy < 4; dy++) {
        // Walls above the well, the floor below it, and the row in between.
        uint14 well = y >= H - dy ? 0x3FFF : WALLS;
        for (int r = 0; r < H; r++)
            well |= y == r - dy ? (uint14)rows[r] << 4 : 0;
        if ((placed(m, dy, x) & well) != 0) return false;
    }
    return true;
}

void tick() {
    seed = seed << 1 | (seed >> 7 ^ seed >> 5 ^ seed >> 4 ^ seed >> 3) & 1;

    // One turn or sideways step per tick, kept only if the piece still fits.
    uint16 m = shape(kind, turn);
    uint2 nt = turn;
    int5 nx = px;
    if (rotate) nt = turn + 1;
    else if (left) nx = px - 1;
    else if (right) nx = px + 1;
    uint16 moved = rotate ? shape(kind, nt) : m;
    if (fits(moved, nx, py)) { turn = nt; px = nx; m = moved; }

    // Gravity: fall, or lock the piece and bring the next one.
    fall++;
    if (down || fall == 0) {
        if (fits(m, px, py + 1)) py++;
        else {
            if (py <= 0) {
                // Stuck at the top: the well is full, start over.
                for (int r = 0; r < H; r++) rows[r] = 0;
            } else {
                uint6 add[4];
                for (int dy = 0; dy < 4; dy++) add[dy] = (uint6)(placed(m, dy, px) >> 4);
                for (int r = 0; r < H; r++) {
                    uint6 lock = 0;
                    for (int dy = 0; dy < 4; dy++) lock |= py == r - dy ? add[dy] : 0;
                    rows[r] |= lock;
                }
            }
            uint3 k = (uint3)seed;
            if (k == 7) k = (uint3)(seed >> 4) & 3;
            kind = k; turn = 0; px = 1; py = 0;
        }
    }

    // Clear the lowest full row; everything above it moves down one.
    bool found = false;
    for (int r = H - 1; r >= 0; r--) {
        if (rows[r] == 63) found = true;
        if (found) rows[r] = r > 0 ? rows[r - 1] : 0;
    }
}

void draw() {
    uint16 m = shape(kind, turn);
    uint14 piece[4];
    for (int dy = 0; dy < 4; dy++) piece[dy] = placed(m, dy, px);
    for (int y = 0; y < H; y++) {
        uint14 here = 0;
        for (int dy = 0; dy < 4; dy++) here |= py == y - dy ? piece[dy] : 0;
        for (int x = 0; x < W; x++)
            pixel(x, y, (rows[y] >> x & 1) | (here >> (x + 4) & 1));
    }
}
)MINEC"},
    {"Breakout",R"MINEC(// Breakout: bounce the ball off the paddle and clear the bricks.
#display 8 10
input hold left;
input hold right;

uint8 bricks[3] = {255, 255, 255};   // rows 0..2; bit x is column x
uint3 paddle = 3;    // left cell of the two-cell paddle on row 9
uint3 bx = 4;
uint4 by = 6;
bool right_ = true;  // the ball moves right
bool down_ = false;  // the ball moves down
bool slow = false;   // the ball moves every other tick
uint2 wait = 0;      // a short pause after a miss

void tick() {
    if (left && paddle > 0) paddle--;
    else if (right && paddle < 6) paddle++;
    slow = !slow;
    if (wait > 0) { wait--; return; }
    if (!slow) return;

    // Side and top walls turn the ball around.
    if (bx == 0) right_ = true;
    if (bx == 7) right_ = false;
    if (by == 0) down_ = true;
    uint3 nx = right_ ? bx + 1 : bx - 1;
    uint4 ny = down_ ? by + 1 : by - 1;

    // A brick in the way breaks and the ball bounces: the one above or below
    // turns it vertically, else the one beside it sideways, else the one on
    // the diagonal both ways.
    bool ahead = false;
    bool beside = false;
    bool corner = false;
    for (int r = 0; r < 3; r++) {
        ahead |= ny == r && (bricks[r] >> bx & 1) != 0;
        beside |= by == r && (bricks[r] >> nx & 1) != 0;
        corner |= ny == r && (bricks[r] >> nx & 1) != 0;
    }
    if (ahead || beside || corner) {
        for (int r = 0; r < 3; r++) {
            if (ahead) { if (ny == r) bricks[r] &= ~((uint8)1 << bx); }
            else if (beside) { if (by == r) bricks[r] &= ~((uint8)1 << nx); }
            else if (ny == r) bricks[r] &= ~((uint8)1 << nx);
        }
        if (!beside || ahead) down_ = !down_;
        if (!ahead) right_ = !right_;
        return;
    }

    // The paddle: its left half sends the ball left, its right half right.
    if (down_ && by == 8) {
        if (bx == paddle || nx == paddle) { down_ = false; right_ = false; return; }
        if (bx == paddle + 1 || nx == paddle + 1) { down_ = false; right_ = true; return; }
    }
    if (ny >= 10) {
        // Missed: serve again, and refill the wall once it is empty.
        bx = 4; by = 6; down_ = false; wait = 3;
        if (bricks[0] == 0 && bricks[1] == 0 && bricks[2] == 0)
            for (int r = 0; r < 3; r++) bricks[r] = 255;
        return;
    }
    bx = nx;
    by = ny;
}

void draw() {
    for (int y = 0; y < 10; y++)
        for (int x = 0; x < 8; x++) {
            bool brick = y < 3 && (bricks[y < 3 ? y : 0] >> x & 1) != 0;
            bool pad = y == 9 && (paddle == x || paddle + 1 == x);
            pixel(x, y, brick || pad || (bx == x && by == y));
        }
}
)MINEC"},
    {"Invaders",R"MINEC(// Invaders: move with left / right and fire at the marching rows.
#display 8 8
input hold left;
input hold right;
input press fire;

uint8 rows[2] = {0x3C, 0x3C};   // the invaders; bit x is column x
uint3 top = 0;                  // row of the first invader row
bool east = true;               // marching direction
uint3 march = 0;                // they step every eighth tick
uint3 player = 3;               // on row 7
uint3 sx = 0;                   // the shot
uint3 sy = 0;
bool shot = false;

void restart() {
    rows[0] = 0x3C; rows[1] = 0x3C;
    top = 0; east = true; shot = false;
}

void tick() {
    if (left && player > 0) player--;
    else if (right && player < 7) player++;
    if (fire && !shot) { shot = true; sx = player; sy = 6; }

    // The shot flies up one row a tick and removes the invader it reaches.
    if (shot) {
        for (int r = 0; r < 2; r++) {
            bool there = sy == top + r && (rows[r] >> sx & 1) != 0;
            if (there) { rows[r] &= ~((uint8)1 << sx); shot = false; }
        }
        if (shot) { if (sy == 0) shot = false; else sy--; }
    }

    // The block marches sideways and steps down at the edges.
    march++;
    if (march == 0) {
        uint8 all = rows[0] | rows[1];
        if (east && (all & 0x80) != 0) { east = false; top++; }
        else if (!east && (all & 1) != 0) { east = true; top++; }
        else if (east) { rows[0] <<= 1; rows[1] <<= 1; }
        else { rows[0] >>= 1; rows[1] >>= 1; }
    }
    // Cleared, or landed on the player's row: a new wave.
    if ((rows[0] | rows[1]) == 0 || top >= 5) restart();
}

void draw() {
    for (int j = 0; j < 8; j++)
        for (int i = 0; i < 8; i++) {
            bool invader = (top == j && (rows[0] >> i & 1) != 0) || (top + 1 == j && (rows[1] >> i & 1) != 0);
            bool ship = j == 7 && player == i;
            pixel(i, j, invader || ship || (shot && sx == i && sy == j));
        }
}
)MINEC"},
    {"Flappy",R"MINEC(// Flappy: press flap to rise, fly through the gaps in the pipes.
#display 8 8
input press flap;

const int GAP = 3;     // gap height

uint3 y = 3;           // the bird, in column 1
bool lift = false;     // a flap waits for the next move
uint3 pipe = 7;        // the pipe's column
uint3 gap = 2;         // top row of the gap
bool slow = false;
uint3 crash = 0;       // flashes after a crash
uint8 seed = 0xA7;

void tick() {
    seed = seed << 1 | (seed >> 7 ^ seed >> 5 ^ seed >> 4 ^ seed >> 3) & 1;
    if (flap) lift = true;
    if (crash > 0) {
        crash--;
        if (crash == 0) { y = 3; pipe = 7; }
        return;
    }
    slow = !slow;
    if (!slow) return;

    // The bird rises two rows after a flap and falls one otherwise.
    if (lift) y = y > 2 ? y - 2 : 0;
    else if (y < 7) y++;
    lift = false;

    // The pipe moves left; a new one comes in with a random gap.
    if (pipe == 0) {
        pipe = 7;
        gap = (uint3)(seed & 3) + 1;
    } else pipe--;

    if (pipe == 1 && (y < gap || y >= gap + GAP)) crash = 6;
}

void draw() {
    for (int j = 0; j < 8; j++)
        for (int i = 0; i < 8; i++) {
            bool wall = pipe == i && (j < gap || j >= gap + GAP);
            bool bird = i == 1 && y == j;
            pixel(i, j, crash > 0 ? (crash & 1) != 0 : wall || bird);
        }
}
)MINEC"},
    {"Snake",R"MINEC(// Snake on an 8 x 8 torus. The body is a shift register of positions plus an
// occupancy grid, so moving touches only the new head and the old tail.
#display 8 8
input hold left;
input hold right;
input hold up;
input hold down;

const int MAXLEN = 12;

uint3 sx[12] = {3, 2, 1};   // segment 0 is the head
uint3 sy[12] = {3, 3, 3};
bool occ[64] = {0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0,
                0, 1, 1, 1};
uint4 len = 3;
uint2 dir = 0;              // 0 right, 1 down, 2 left, 3 up
uint2 moved = 0;            // the direction of the last step: no turning back on it
uint2 clock = 0;            // the snake moves every fourth tick
uint6 food = 45;
bool hungry = false;        // looking for a free cell for the next food
uint8 seed = 0x5a;
bool dead = false;
uint4 rest = 0;             // ticks the dead snake blinks before a key restarts

uint6 cell(uint3 x, uint3 y) { return (uint6)y * 8 + x; }

void reset() {
    for (int i = 0; i < 64; i++) occ[i] = false;
    for (int k = 0; k < MAXLEN; k++) { sx[k] = 0; sy[k] = 0; }
    sx[0] = 3; sx[1] = 2; sx[2] = 1;
    sy[0] = 3; sy[1] = 3; sy[2] = 3;
    occ[27] = true; occ[26] = true; occ[25] = true;
    len = 3; dir = 0; moved = 0; food = 45; hungry = false; dead = false;
}

void tick() {
    seed = seed << 1 | (seed >> 7 ^ seed >> 5 ^ seed >> 4 ^ seed >> 3) & 1;
    clock++;
    if (dead) {
        if (rest > 0) rest--;
        else if (left || right || up || down) reset();
        return;
    }
    if (right && moved != 2) dir = 0;
    else if (down && moved != 3) dir = 1;
    else if (left && moved != 0) dir = 2;
    else if (up && moved != 1) dir = 3;

    if (hungry) {
        uint6 spot = (uint6)seed;
        if (!occ[spot]) { food = spot; hungry = false; }
    }

    if (clock != 0) return;

    uint3 nx = sx[0];
    uint3 ny = sy[0];
    if (dir == 0) nx++;
    if (dir == 1) ny++;
    if (dir == 2) nx--;
    if (dir == 3) ny--;
    uint6 head = cell(nx, ny);
    bool eat = !hungry && head == food;

    // The tail moves on unless the snake grows (at full length it moves on anyway),
    // so the head may take the cell the tail leaves.
    uint6 tail = cell(sx[len - 1], sy[len - 1]);
    bool frees = !eat || len == MAXLEN;
    if (occ[head] && !(frees && head == tail)) { dead = true; rest = 12; return; }
    if (frees) occ[tail] = false;
    occ[head] = true;
    for (int k = MAXLEN - 1; k > 0; k--) { sx[k] = sx[k - 1]; sy[k] = sy[k - 1]; }
    sx[0] = nx;
    sy[0] = ny;
    moved = dir;
    if (eat) {
        if (len < MAXLEN) len++;
        hungry = true;
    }
}

// The food blinks; so does the whole snake once it is dead.
void draw() {
    bool blink = clock >= 2;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            bool on = (occ[y * 8 + x] && (!dead || blink)) || (!hungry && food == y * 8 + x && blink);
            pixel(x, y, on);
        }
}
)MINEC"},
    {"Pong",R"MINEC(// Pong: keep the ball in play with the paddle on the left (up / down).
#display 8 6
input hold up;
input hold down;

uint3 paddle = 2;
uint3 bx = 4;
uint3 by = 2;
bool dx = false;    // moving right
bool dy = true;     // moving down
bool slow = false;
uint2 lost = 0;

void tick() {
    if (up && paddle > 0) paddle--;
    else if (down && paddle < 4) paddle++;
    slow = !slow;
    if (lost > 0) {
        lost--;
        bx = 4;
        by = 2;
    } else if (slow) {
        if (by == 0) dy = true;
        else if (by == 5) dy = false;
        if (bx == 7) dx = false;
        else if (bx == 1 && (by == paddle || by == paddle + 1)) dx = true;
        if (bx == 0) lost = 3;
        else {
            if (dx) bx++; else bx--;
            if (dy) by++; else by--;
        }
    }
}

void draw() {
    for (int j = 0; j < 6; j++)
        for (int i = 0; i < 8; i++)
            pixel(i, j, (bx == i && by == j) || (i == 0 && (paddle == j || paddle + 1 == j)) || (lost > 0 && j == 0));
}
)MINEC"},
    {"Catch",R"MINEC(// Catch: move the paddle to catch the falling ball. A miss flashes the screen.
#display 6 8
input hold left;
input hold right;

uint3 paddle = 2;
uint3 ballx = 1;
uint3 bally = 0;
uint5 rng = 21;
bool slow = false;
uint2 flash = 0;

void tick() {
    rng = rng << 1 | (rng >> 4 ^ rng >> 2) & 1;
    if (left && paddle > 0) paddle--;
    else if (right && paddle < 4) paddle++;
    slow = !slow;
    if (flash > 0) flash--;
    else if (slow) {
        if (bally < 7) bally++;
        else {
            if (ballx != paddle && ballx != paddle + 1) flash = 3;
            bally = 0;
            ballx = (uint3)(rng & 3);
            if (rng >> 4 & 1) ballx += 2;
        }
    }
}

void draw() {
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 8; j++)
            pixel(i, j, flash > 0 || (ballx == i && bally == j) || (j == 7 && (paddle == i || paddle + 1 == i)));
}
)MINEC"},
    {"Paint",R"MINEC(// Paint: arrow keys move the cursor, paint toggles the pixel under it.
#display 6 6
input press left;
input press right;
input press up;
input press down;
input press paint;

uint3 x = 2;
uint3 y = 2;
bool canvas[36];
bool blink = false;

void tick() {
    if (paint) canvas[(uint6)y * 6 + x] = !canvas[(uint6)y * 6 + x];
    if (left && x > 0) x--;
    if (right && x < 5) x++;
    if (up && y > 0) y--;
    if (down && y < 5) y++;
    blink = !blink;
}

void draw() {
    for (int j = 0; j < 6; j++)
        for (int i = 0; i < 6; i++)
            pixel(i, j, canvas[j * 6 + i] ^ (blink && x == i && y == j));
}
)MINEC"},
    {"Life",R"MINEC(// Conway's Game of Life on an 8 x 8 torus with a cursor to draw cells.
// Arrow keys move the cursor, A toggles the cell under it, B runs or pauses.
#display 8 8
input press left;
input press right;
input press up;
input press down;
input press toggle;
input press run;

bool cell[64] = {0, 1, 0, 0, 0, 0, 0, 0,
                 0, 0, 1, 0, 0, 0, 0, 0,
                 1, 1, 1};          // a glider
uint3 cx = 4;
uint3 cy = 4;
bool running = true;
uint2 clock = 0;
bool blink = false;

void tick() {
    blink = !blink;
    if (left) cx--;
    if (right) cx++;
    if (up) cy--;
    if (down) cy++;
    if (run) running = !running;
    if (toggle) cell[(uint6)cy * 8 + cx] = !cell[(uint6)cy * 8 + cx];
    clock++;
    if (!running || clock != 0) return;

    // Column sums of three cells are shared by the three cells beside them;
    // the 3 x 3 total t (self included) decides: alive next iff t == 3,
    // or t == 4 and alive now.
    uint2 col[64];
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            col[y * 8 + x] = (uint2)cell[((y + 7) & 7) * 8 + x] + cell[y * 8 + x] + cell[((y + 1) & 7) * 8 + x];
        }
    bool next[64];
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            uint4 t = (uint4)col[y * 8 + ((x + 7) & 7)] + col[y * 8 + x] + col[y * 8 + ((x + 1) & 7)];
            next[y * 8 + x] = t == 3 || (t == 4 && cell[y * 8 + x]);
        }
    for (int i = 0; i < 64; i++) cell[i] = next[i];
}

void draw() {
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            bool here = cx == x && cy == y;
            pixel(x, y, here ? cell[y * 8 + x] ^ blink : cell[y * 8 + x]);
        }
}
)MINEC"},
    {"Langton's Ant",R"MINEC(// Langton's ant on a 12 x 12 torus: on a white cell turn right, on a black
// cell turn left, flip the cell and step forward. Run pauses and resumes.
#display 12 12
input press run;

const int N = 12;

bool grid[144];
uint4 ax = 6;
uint4 ay = 6;
uint2 dir = 0;          // 0 up, 1 right, 2 down, 3 left
bool paused = false;
bool blink = false;

void tick() {
    blink = !blink;
    if (run) paused = !paused;
    if (paused) return;
    uint8 here = (uint8)ay * N + ax;
    bool black = grid[here];
    if (black) dir--; else dir++;
    grid[here] = !black;
    if (dir == 0) ay = ay == 0 ? N - 1 : ay - 1;
    if (dir == 1) ax = ax == N - 1 ? 0 : ax + 1;
    if (dir == 2) ay = ay == N - 1 ? 0 : ay + 1;
    if (dir == 3) ax = ax == 0 ? N - 1 : ax - 1;
}

void draw() {
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++)
            pixel(x, y, grid[y * N + x] ^ (blink && ax == x && ay == y));
}
)MINEC"},
    {"Automata",R"MINEC(// Elementary cellular automata on a ring of 16 cells: each tick adds a
// generation at the bottom and the picture scrolls up. Rule switches between
// rules 30, 90 and 110; reset starts again from one cell.
#display 16 12
input press rule;
input press reset;

uint16 rows[12];
uint2 which = 0;       // 0: rule 30, 1: rule 90, 2: rule 110
bool started = false;

uint16 next(uint16 c, uint2 r) {
    uint16 l = c << 1 | c >> 15;    // each cell's left neighbour
    uint16 n = c >> 1 | c << 15;    // and right neighbour
    if (r == 0) return l ^ (c | n);
    if (r == 1) return l ^ n;
    return (c ^ n) | (c & ~l);
}

void tick() {
    if (rule) which = which == 2 ? 0 : which + 1;
    if (reset || rule || !started) {
        for (int i = 0; i < 12; i++) rows[i] = 0;
        rows[11] = 0x0100;
        started = true;
        return;
    }
    uint16 last = rows[11];
    for (int i = 0; i < 11; i++) rows[i] = rows[i + 1];
    rows[11] = next(last, which);
}

void draw() {
    for (int y = 0; y < 12; y++)
        for (int x = 0; x < 16; x++)
            pixel(x, y, rows[y] >> x & 1);
}
)MINEC"},
    {"Mandelbrot",R"MINEC(// Mandelbrot set, one iteration per tick, scanning the screen pixel by pixel.
// Fixed point: int8 with 4 fraction bits (1.0 = 16). The set is symmetric
// about the real axis, so only the top half is computed and draw() mirrors it.
#display 16 16
input press zoom;     // switch between the whole set and a closer view

const int MAX = 10;   // iterations before a point counts as inside

bool img[128];        // rows 0..7, the top half
bool line[16];        // the current row, shifted in from the right
uint4 px = 0;
uint3 py = 0;
int8 zr = 0;
int8 zi = 0;
uint4 n = 0;
bool zoomed = false;

// v * v for |v| < 256, through the magnitude so the multiplier stays unsigned.
uint16 square(int9 v) {
    uint9 m = v < 0 ? -v : v;
    return (uint16)m * m;
}

// The same square modulo 2^12: enough where only bits 4..11 are kept.
uint12 square12(int9 v) {
    uint9 m = v < 0 ? -v : v;
    return (uint12)m * m;
}

void tick() {
    // Pixel centres: x from -2.3 (or -1.75 zoomed), y from just above the axis upwards.
    int8 cr = zoomed ? (int8)px * 2 - 28 : (int8)px * 3 - 37;
    int8 ci = zoomed ? -((int8)(7 - py) * 2 + 1) : -((int8)(7 - py) * 3 + 1);
    uint16 a = square(zr);          // zr^2 in 1/256 units
    uint16 b = square(zi);
    bool escaped = a + b > 1024;    // |z|^2 > 4
    if (escaped || n == MAX) {
        for (int k = 0; k < 15; k++) line[k] = line[k + 1];
        line[15] = !escaped;
        if (px == 15)
            for (int r = 0; r < 8; r++)
                if (py == r)
                    for (int x = 0; x < 16; x++) img[r * 16 + x] = line[x];
        zr = 0;
        zi = 0;
        n = 0;
        px++;
        if (px == 0) py++;
    } else {
        // New z in 12-bit arithmetic: the int8 result keeps bits 4..11 only.
        int12 cross = (int12)square12((int9)zr + zi) - (int12)a - (int12)b;   // 2 zr zi
        zr = (int8)(((int12)a - (int12)b) >> 4) + cr;
        zi = (int8)(cross >> 4) + ci;
        n++;
    }
    if (zoom) {
        zoomed = !zoomed;
        px = 0; py = 0; zr = 0; zi = 0; n = 0;
    }
}

void draw() {
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            pixel(x, y, img[(y < 8 ? y : 15 - y) * 16 + x]);
}
)MINEC"},
    {"Counter",R"MINEC(// A four-digit decimal counter in a 3 x 5 font. Start pauses and resumes,
// reset sets it back to 0000.
#display 15 7
input press start;
input press reset;

uint4 digits[4];       // most significant first
bool running = true;

// The 3 x 5 picture of a digit: bit row * 3 + column.
uint15 glyph(uint4 n) {
    return n == 0 ? 0x7B6F : n == 1 ? 0x749A : n == 2 ? 0x73E7 : n == 3 ? 0x79E7 : n == 4 ? 0x49ED
         : n == 5 ? 0x79CF : n == 6 ? 0x7BCF : n == 7 ? 0x4927 : n == 8 ? 0x7BEF : 0x79EF;
}

void tick() {
    if (start) running = !running;
    if (reset) {
        for (int i = 0; i < 4; i++) digits[i] = 0;
        return;
    }
    if (!running) return;
    // Add one, carrying from the last digit to the first.
    bool carry = true;
    for (int i = 3; i >= 0; i--) {
        if (carry) {
            if (digits[i] == 9) digits[i] = 0;
            else { digits[i]++; carry = false; }
        }
    }
}

void draw() {
    for (int k = 0; k < 4; k++) {
        uint15 g = glyph(digits[k]);
        for (int row = 0; row < 5; row++)
            for (int col = 0; col < 3; col++)
                pixel(k * 4 + col, 1 + row, g >> (row * 3 + col) & 1);
    }
}
)MINEC"},
};
}
const char* mineCExample(int i) {return i>=0 && i<static_cast<int>(std::size(cExamples))?cExamples[i][1]:nullptr;}
const char* mineCExampleName(int i) {return i>=0 && i<static_cast<int>(std::size(cExamples))?cExamples[i][0]:nullptr;}
}
