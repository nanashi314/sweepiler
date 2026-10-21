#include "ClassicCircuits.h"
#include "ClassicKernels.h"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
namespace ims {
bool isClassicCircuit(CircuitKind k) { return std::find(classicKinds.begin(),classicKinds.end(),k)!=classicKinds.end(); }
int classicBit(const std::string& s) {
    if(s=="*") return 1;
    if(s.size()==1 && s[0]>='0' && s[0]<='8') return 0;
    return -1;
}
int classicInput(const Board& b,Coord p) { return b.safe(p)?0:classicBit(b.get(p)); }
void setClassicInput(Board& b,Coord p,int bit) {
    if(bit < -1 || bit>1) throw std::invalid_argument("Classic input must be -1, 0 or 1");
    if(bit==0) b.assumeSafe(p); else b.set(p,bit==1?"*":"");
}
void pinClassicPort(Problem& p,const Port& port,int bit) {
    if(bit<0 || bit>1) throw std::invalid_argument("Classic port must be 0 or 1");
    std::erase_if(p.pins,[&](const Pin& pin){return pin.coord==port.coord;});
    std::erase_if(p.exclusions,[&](const Exclusion& e){return e.coord==port.coord && e.symbol=="*";});
    if(bit) p.pins.push_back({port.coord,"*"}); else p.exclusions.push_back({port.coord,"*"});
}
Circuit makeClassicCircuit(CircuitKind kind) {
    if(!isClassicCircuit(kind)) throw std::invalid_argument("Unknown classic circuit");
    if(kind>=CircuitKind::ClassicNor) return makeClassicKernelCircuit(kind);
    struct Less { bool operator()(Coord a,Coord b) const { return a.y<b.y || (a.y==b.y && a.x<b.x); } };
    std::set<Coord,Less> candidates,ones,zeros;
    auto path=[&](std::initializer_list<Coord> corners) {
        std::optional<Coord> last;
        for(auto p:corners) {
            candidates.insert(p);
            if(last) {
                auto q=*last;
                if((q.x!=p.x && q.y!=p.y) || (std::abs(q.x-p.x)+std::abs(q.y-p.y))%2) throw std::logic_error("Invalid classic routing segment");
                int dx=q.x==p.x?0:q.x<p.x?2:-2,dy=q.y==p.y?0:q.y<p.y?2:-2;
                while(q!=p) { ones.insert({q.x+dx/2,q.y+dy/2}); q={q.x+dx,q.y+dy}; candidates.insert(q); }
            }
            last=p;
        }
    };
    Circuit c; c.kind=kind;
    if(kind==CircuitKind::ClassicBend) {
        path({{0,0},{4,0},{4,4}}); c.outputs={{"Y",{4,4}}};
    } else if(kind==CircuitKind::ClassicFanout) {
        path({{0,0},{8,0}}); path({{4,0},{4,4}}); c.outputs={{"Y",{8,0}},{"Z",{4,4}}};
    } else {
        int end=kind==CircuitKind::ClassicWire?8:6; path({{0,0},{end,0}}); c.outputs={{"Y",{end,0}}};
    }
    int64_t maxX=0,maxY=0;
    for(auto p:candidates) { maxX=std::max(maxX,p.x); maxY=std::max(maxY,p.y); }
    c.problem.origin={-2,-2}; c.problem.width=static_cast<int>(maxX)+5; c.problem.height=static_cast<int>(maxY)+5;
    c.problem.rules=RuleTable::classic(); c.problem.boundary=Boundary::FixedHalo; c.problem.haloSymbol=0;
    c.problem.timeoutMs=10000;
    auto near=[](Coord a,Coord b) { return std::abs(a.x-b.x)<=1 && std::abs(a.y-b.y)<=1; };
    // Every zero is chosen solely by geometry, before any input value is fixed.
    for(int y=-2;y<=maxY+2;++y) for(int x=-2;x<=maxX+2;++x) {
        Coord p{x,y}; bool touches=false;
        for(auto q:candidates) touches|=near(p,q);
        if(!touches) zeros.insert(p);
    }
    for(auto p:ones) {
        int n=0; for(auto q:candidates) n+=near(p,q);
        if(n!=2 || candidates.contains(p) || zeros.contains(p)) throw std::logic_error("A routing 1 must touch exactly two candidates");
    }
    // Do not assume that off-route cells are walls: force them safe using real zero clues.
    for(int y=-2;y<=maxY+2;++y) for(int x=-2;x<=maxX+2;++x) {
        Coord p{x,y}; if(candidates.contains(p) || ones.contains(p) || zeros.contains(p)) continue;
        bool guarded=false; for(auto z:zeros) guarded|=near(p,z);
        if(!guarded) throw std::logic_error("Unprotected off-route mine candidate");
    }
    for(auto p:zeros) c.problem.pins.push_back({p,"0"});
    for(auto p:ones) c.problem.pins.push_back({p,"1"});
    c.signals.assign(candidates.begin(),candidates.end()); c.inputs={{"A",{0,0}}};
    c.provenance="Independent ordinary Minesweeper routing: revealed 1 clues and zero guards, no extended symbols";
    return c;
}
}
