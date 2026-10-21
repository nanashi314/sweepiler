#include "MineC.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
using namespace ims;
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
size_t checks{};
// The compiled NOR netlist and the independent interpreter must agree on every
// next-state bit and pixel, from reachable states and from random ones.
void agree(const std::string& source,const std::string& name,size_t ticks,bool randomStates,unsigned seed) {
    auto start=std::chrono::steady_clock::now();
    auto circuit=compileMineC(source,name); MineCReference ref(parseMineC(source));
    double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::mt19937_64 rng(seed); auto state=ref.initial();
    require(ref.pack(state)==circuit.initial,name+": initial state");
    for(size_t t=0;t<ticks;++t) {
        if(randomStates) {std::vector<uint8_t> bits; for(size_t i=0;i<circuit.stateBits;++i) bits.push_back(static_cast<uint8_t>(rng()&1)); state=ref.unpack(bits);}
        std::vector<uint8_t> keys; for(size_t k=0;k<circuit.keys.size();++k) keys.push_back(static_cast<uint8_t>(rng()%3==0));
        auto bits=ref.pack(state); bits.insert(bits.end(),keys.begin(),keys.end());
        auto signals=circuit.net.evaluate(bits);
        std::vector<uint8_t> display; auto next=ref.step(state,keys,&display);
        auto expected=ref.pack(next); expected.insert(expected.end(),display.begin(),display.end());
        require(expected.size()==circuit.net.outputs.size(),name+": output count");
        for(size_t o=0;o<expected.size();++o) require(circuit.net.value(signals,circuit.net.outputs[o].second)==expected[o],name+": "+circuit.net.outputs[o].first+" differs at tick "+std::to_string(t));
        checks+=expected.size(); state=next;
    }
    std::cout<<name<<": "<<circuit.net.gates.size()<<" NOR gates, "<<circuit.stateBits<<" state bits, compiled in "<<static_cast<int>(ms)<<" ms, "<<ticks<<(randomStates?" random":"")<<" ticks agree"<<std::endl;
}
// A sample's own game rules, on the reference interpreter with random keys:
// check(before, after) sees each tick's state through a name lookup.
struct Names {
    std::map<std::string,size_t> at;
    explicit Names(const MineCProgram& p) {size_t k=0; for(const auto& g:p.globals) {at[g.name]=k; k+=static_cast<size_t>(std::max(1,g.count));}}
};
template<class Check> void rules(const char* name,size_t ticks,Check check) {
    int index=0; while(mineCExampleName(index) && std::string(mineCExampleName(index))!=name) ++index;
    require(mineCExample(index)!=nullptr,std::string("no sample ")+name);
    MineCReference ref(parseMineC(mineCExample(index))); Names n(ref.program());
    std::mt19937_64 rng(7); auto state=ref.initial();
    for(size_t t=0;t<ticks;++t) {
        std::vector<uint8_t> keys; for(size_t k=0;k<ref.program().keys.size();++k) keys.push_back(static_cast<uint8_t>(rng()%4==0));
        auto next=ref.step(state,keys);
        auto get=[&](const std::vector<int64_t>& s){return [&s,&n](const std::string& v,int i=0){return s[n.at.at(v)+static_cast<size_t>(i)];};};
        check(get(state),get(next),std::string(name)+" tick "+std::to_string(t)+": ");
        state=next;
    }
    std::cout<<name<<": game rules hold for "<<ticks<<" ticks"<<std::endl;
}
void gameRules() {
    // Breakout: the ball is never inside a brick, and a brick breaks only next to the ball,
    // one at a time, the one straight ahead or beside it before the one on the corner.
    rules("Breakout",20000,[](auto before,auto after,const std::string& at){
        int broken=0;
        for(int r=0;r<3;++r) for(int c=0;c<8;++c) {
            bool was=(before("bricks",r)>>c)&1,is=(after("bricks",r)>>c)&1;
            if(was && !is) {
                ++broken; const int64_t bx=before("bx"),by=before("by");
                require(std::abs(c-bx)<=1 && std::abs(r-by)<=1,at+"a brick away from the ball broke");
                // A corner brick breaks only when the cells straight ahead and beside are clear.
                auto brick=[&](int64_t col,int64_t row){return row>=0 && row<3 && ((before("bricks",static_cast<int>(row))>>col)&1);};
                if(c!=bx && r!=by) require(!brick(bx,r) && !brick(c,by),at+"a corner brick broke past a brick in the way");
            }
            if(is) require(!(after("bx")==c && after("by")==r),at+"the ball is inside a brick");
        }
        require(broken<=1,at+"more than one brick broke");
    });
    // Snake: the occupancy grid holds exactly the body, the head never turns back onto
    // the neck, and the food is never on the body.
    rules("Snake",20000,[](auto before,auto after,const std::string& at){
        int64_t len=after("len"),cells=0; for(int i=0;i<64;++i) cells+=after("occ",i);
        require(cells==len,at+"the grid holds "+std::to_string(cells)+" cells for length "+std::to_string(len));
        for(int k=0;k<len;++k) require(after("occ",static_cast<int>(after("sy",k)*8+after("sx",k)))==1,at+"a segment is off the grid");
        if(after("sx",0)!=before("sx",0) || after("sy",0)!=before("sy",0))
            require(!(after("sx",0)==before("sx",1) && after("sy",0)==before("sy",1)),at+"the head turned back");
        if(!after("hungry")) require(after("occ",static_cast<int>(after("food")))==0,at+"the food is on the body");
    });
}
void rejects(const std::string& source,const std::string& fragment) {
    try {compileMineC(source);} catch(const std::invalid_argument& e) {
        require(std::string(e.what()).find(fragment)!=std::string::npos,std::string("wrong error: ")+e.what()); return;
    }
    throw std::runtime_error("accepted bad program: "+fragment);
}
int main(int argc,char** argv){try{
    if(argc>3 && std::string(argv[1])=="run") {
        // mine_c_tests run INDEX TICKS: the interpreter's display after TICKS ticks with no keys.
        MineCReference ref(parseMineC(mineCExample(std::atoi(argv[2])))); const auto& p=ref.program();
        auto state=ref.initial(); std::vector<uint8_t> display,keys(p.keys.size(),0);
        for(int t=0;t<std::atoi(argv[3]);++t) state=ref.step(state,keys,&display);
        for(int y=0;y<p.height;++y) {for(int x=0;x<p.width;++x) std::cout<<(display[static_cast<size_t>(x+p.width*y)]?"##":". "); std::cout<<'\n';}
        return 0;
    }
    if(argc>1 && std::string(argv[1])=="stats") {
        // Fanout profile of each example (placement and routing difficulty).
        for(int i=0;mineCExample(i);++i) {
            const auto net=compileMineC(mineCExample(i)).net; std::vector<int> f(net.inputs.size()+net.gates.size());
            for(const auto& g:net.gates) for(auto x:g) if(!x.constant()) ++f[static_cast<size_t>(x.id)];
            for(const auto& out:net.outputs) if(!out.second.constant()) ++f[static_cast<size_t>(out.second.id)];
            auto sorted=f; std::sort(sorted.rbegin(),sorted.rend()); size_t pins=0,wide=0; for(int v:f) {pins+=static_cast<size_t>(v); if(v>16) wide+=static_cast<size_t>(v);}
            std::cout<<mineCExampleName(i)<<": "<<net.gates.size()<<" gates, "<<pins<<" pins, "<<wide<<" on nets wider than 16; widest";
            for(size_t k=0;k<10 && k<sorted.size();++k) std::cout<<' '<<sorted[k];
            std::cout<<std::endl;
        }
        return 0;
    }
    if(argc>1) {
        // mine_c_tests FILE: compile one program and report its size.
        std::FILE* f=nullptr; if(fopen_s(&f,argv[1],"rb") || !f) throw std::runtime_error("cannot open file");
        std::string s; char buf[4096]; size_t n; while((n=std::fread(buf,1,sizeof buf,f))>0) s.append(buf,n); std::fclose(f);
        agree(s,argv[1],200,false,1); agree(s,argv[1],200,true,2); return 0;
    }
    for(int i=0;mineCExample(i);++i) {
        // Mandelbrot needs about a thousand ticks to finish the first picture.
        agree(mineCExample(i),mineCExampleName(i),i==0?3000:1500,false,static_cast<unsigned>(i+1));
        agree(mineCExample(i),mineCExampleName(i),300,true,static_cast<unsigned>(i+11));
    }
    gameRules();
    // Signed and unsigned arithmetic, division, variable shifts, casts and ?:.
    const char* arithmetic=R"(
#display 4 2
input hold k;
input press j;
int8 a = -7;
uint8 b = 200;
int5 c = 9;
uint3 s = 2;
int16 m = 0;
uint8 q = 0;
int8 r = 0;
bool f = false;
void tick() {
    m = (int16)a * c + b / (s + 1) - (b % 7);
    q = b / (uint8)(a + 8);       // can divide by zero
    r = a / c + a % (int8)c;      // signed: rounds toward zero
    a = a + (k ? 3 : -5) - (a >> s);
    b = (b << s) ^ (b >> (s + 3)) | (uint8)~c;
    c = -c + (int5)(b >> 4) * 3;
    f = (a < b) != (c >= (int5)a) && a <= -1 || !(b > 128);
    s = s + (j ? 1 : 0) + (uint3)f;
}
void draw() {
    for (int i = 0; i < 4; i++) {
        pixel(i, 0, a >> i & 1);
        pixel(i, 1, (m >> (i * 3)) & 1);
    }
}
)";
    agree(arithmetic,"arithmetic",3000,false,7); agree(arithmetic,"arithmetic",3000,true,8);
    // Functions with early returns in loops, break/continue, local arrays,
    // variable indices for reads and writes, variable pixel coordinates.
    const char* control=R"(
#display 5 3
input hold go;
input press flip;
uint4 v[6] = {1, 5, 9, 2, 14, 7};
uint3 p = 2;
uint3 x = 1;
uint2 y = 0;
uint8 acc = 0;
uint4 firstAbove(uint4 limit) {
    for (int i = 0; i < 6; i++) {
        if (v[i] > limit) return (uint4)i;
    }
    return 15;
}
uint8 sum(uint3 upto) {
    uint8 t = 0;
    for (int i = 0; i < 6; i++) {
        if (i == 3) continue;
        if (i > upto) break;
        t += v[i];
    }
    return t;
}
void tick() {
    uint4 w[3] = {v[p], (uint4)p, 3};
    if (go) {
        v[p] += w[(uint2)acc % 3];
        p++;
        if (p >= 6) p = 0;
    } else if (flip) {
        for (int i = 0; i < 3; i++) {
            uint4 t = v[i];
            v[i] = v[5 - i];
            v[5 - i] = t;
        }
    }
    acc = acc * 3 + sum(p) + firstAbove(v[(p + 1) % 6]);
    x = (uint3)(acc % 5);
    y = (uint2)(acc >> 6);
    v[x] -= 1;
}
void draw() {
    pixel(x, y, 1);
    pixel(4, 2, acc & 1);
    if (acc > 100) pixel(0, (uint2)(acc % 3), 1);
}
)";
    agree(control,"control",3000,false,9); agree(control,"control",3000,true,10);
    rejects("#display 2 2\ninput hold a;\nuint2 x;\nvoid tick() { y = 1; }","unknown name 'y'");
    rejects("#display 2 2\ninput hold a;\nuint2 x;\nvoid tick() { a = 1; }","inputs cannot be assigned");
    rejects("#display 2 2\ninput hold a;\nuint2 x;\nvoid tick() { for (int i = 0; i < x; i++) x++; }","loop condition must be constant");
    rejects("#display 2 2\ninput hold a;\nuint2 x;\nvoid f() { f(); }\nvoid tick() { f(); }","recursion");
    rejects("#display 2 2\ninput hold a;\nuint2 x;\nvoid tick() { pixel(2, 0, 1); }","outside the display");
    rejects("#display 2 2\ninput hold a;\nuint2 x;\nvoid tick() { x = 1 }","';' expected");
    rejects("#display 2 2\nuint2 x;\nvoid tick() { x = 1; }","1..6 inputs");
    rejects("#display 2 2\ninput hold a;\nuint2 x;\nvoid tick() { while (a) x++; }","while loops");
    std::cout<<checks<<" output comparisons agree"<<std::endl;
    std::cout<<"MineC tests passed"<<std::endl; return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
