#include "BuildControl.h"
#include "ClassicKernels.h"
#include <algorithm>
#include <stdexcept>
namespace ims {
namespace {
Coord at(Coord p,int x,int y) { Coord r; if(!addChecked(p.x,x,r.x) || !addChecked(p.y,y,r.y)) throw std::invalid_argument("Classic kernel coordinate overflow"); return r; }
bool near(Coord a,Coord b) { return a!=b && std::abs(a.x-b.x)<=1 && std::abs(a.y-b.y)<=1; }
}
void ClassicPatch::count(Coord p,int n,std::initializer_list<Coord> neighbours) {
    Requirement r{n,{}};
    for(auto q:neighbours) { if(!near(p,q)) throw std::logic_error("Nonlocal classic count"); r.neighbours.insert(q); variable(q); }
    auto [it,fresh]=counts_.emplace(p,r);
    if(!fresh && (it->second.mines!=n || it->second.neighbours!=r.neighbours)) throw std::logic_error("Classic clue overlap");
}
void ClassicPatch::link(Coord a,Coord b,Coord clue) { count(clue,1,{a,b}); }
void ClassicPatch::wire(std::initializer_list<Coord> corners) {
    std::optional<Coord> last;
    for(auto p:corners) {
        variable(p);
        if(last) {
            auto q=*last;
            if((q.x!=p.x && q.y!=p.y) || (std::abs(q.x-p.x)+std::abs(q.y-p.y))%2) throw std::logic_error("Invalid classic wire segment");
            int dx=q.x==p.x?0:q.x<p.x?2:-2,dy=q.y==p.y?0:q.y<p.y?2:-2;
            while(q!=p) { auto next=at(q,dx,dy); link(q,next,at(q,dx/2,dy/2)); q=next; }
        }
        last=p;
    }
}
std::array<Port,3> ClassicPatch::nor(Coord offset,bool mirrored,bool flipped) {
    auto p=[&](int x,int y){return at(offset,mirrored?-x:x,flipped?-y:y);};
    // Thieme/Basten Fig.1 AND kernel, translated by one row. Corner flags isolate a one-hot kernel.
    // The two input links invert; the two output links preserve phase: Y = NOT(A OR B).
    count(p(2,3),1,{p(1,3),p(2,2),p(3,3),p(2,4)});
    count(p(1,2),1,{p(0,2),p(1,3),p(2,2)});
    count(p(1,4),1,{p(0,4),p(1,3),p(2,4)});
    link(p(-2,2),p(0,2),p(-1,2));
    link(p(-2,4),p(0,4),p(-1,4));
    link(p(3,3),p(5,3),p(4,3));
    link(p(5,3),p(6,4),p(5,4));
    return {{{"A",p(-2,2)},{"B",p(-2,4)},{"Y",p(6,4)}}};
}
std::array<Port,4> ClassicPatch::crossing(Coord offset) {
    auto p=[&](int x,int y){return at(offset,x,y);};
    // Thieme/Basten Fig.2 crossing: four inner corners are one-hot.
    count(p(2,2),1,{p(1,1),p(3,1),p(1,3),p(3,3)});
    count(p(2,1),1,{p(2,0),p(1,1),p(3,1)});
    count(p(1,2),1,{p(0,2),p(1,1),p(1,3)});
    count(p(3,2),1,{p(4,2),p(3,1),p(3,3)});
    count(p(2,3),1,{p(2,4),p(1,3),p(3,3)});
    link(p(4,2),p(6,2),p(5,2)); link(p(2,4),p(2,6),p(2,5));
    return {{{"A",p(0,2)},{"B",p(2,0)},{"Y",p(6,2)},{"Z",p(2,6)}}};
}
Circuit ClassicPatch::finish(CircuitKind kind,std::vector<Port> in,std::vector<Port> out,int padding,bool emitScaffold) const {
    if(padding<1||padding>2)throw std::invalid_argument("Invalid scaffold padding");
    if(variables_.empty()) throw std::logic_error("Empty classic kernel");
    size_t checked=0;for(const auto& [p,r]:counts_) {if(++checked%512==0)buildCheckpoint("Adjacency",checked,counts_.size());
        if(variables_.contains(p)) throw std::logic_error("Clue overlaps classic variable");
        std::set<Coord,ClassicCoordLess> actual;
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) {
            auto q=at(p,dx,dy); if(variables_.contains(q)) actual.insert(q);
        }
        if(actual!=r.neighbours || r.mines<0 || r.mines>static_cast<int>(actual.size())) throw std::logic_error("Unintended kernel variable adjacency at "+std::to_string(p.x)+","+std::to_string(p.y));
    }
    auto first=*variables_.begin(); int64_t minX=first.x,maxX=first.x,minY=first.y,maxY=first.y;
    auto extend=[&](Coord p) { minX=std::min(minX,p.x); maxX=std::max(maxX,p.x); minY=std::min(minY,p.y); maxY=std::max(maxY,p.y); };
    for(auto p:variables_) extend(p); for(const auto& [p,r]:counts_) extend(p);
    Circuit c; c.kind=kind; c.inputs=std::move(in); c.outputs=std::move(out); c.signals.assign(variables_.begin(),variables_.end());
    auto& problem=c.problem; problem.origin=at({minX,minY},-padding,-padding);
    problem.width=static_cast<int>(maxX-minX+2*padding+1); problem.height=static_cast<int>(maxY-minY+2*padding+1);
    problem.rules=RuleTable::classic(); problem.boundary=Boundary::FixedHalo; problem.haloSymbol=problem.rules.index("*"); problem.timeoutMs=10000;
    Layout l; if(auto e=makeLayout(problem,l);!e.empty()) throw std::invalid_argument(e);
    if(!emitScaffold)return c;
    for(int y=1;y<l.height-1;++y) for(int x=1;x<l.width-1;++x) {if(x==1 && y%8==1)buildCheckpoint("Scaffold",y,l.height);
        auto p=l.coord(x,y); if(variables_.contains(p)) continue;
        auto it=counts_.find(p); std::string symbol="*";
        if(it!=counts_.end()) {
            int fixedMines=0;
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) {
                auto q=at(p,dx,dy); fixedMines+=!variables_.contains(q) && !counts_.contains(q);
            }
            int n=fixedMines+it->second.mines;
            if(n<0 || n>8) throw std::logic_error("Invalid ordinary kernel clue"); symbol=std::to_string(n);
        }
        problem.pins.push_back({p,symbol});
    }
    c.provenance="Ordinary one-hot kernel based on Thieme/Basten 2022 Fig.1/2; independent adapters and whole-board verification";
    return c;
}
CellGrid ClassicPatch::finishGrid(int padding) const {
    if(padding<1||padding>2)throw std::invalid_argument("Invalid scaffold padding");
    if(variables_.empty()) throw std::logic_error("Empty classic kernel");
    size_t checked=0;for(const auto& [p,r]:counts_) {if(++checked%4096==0)buildCheckpoint("Adjacency",checked,counts_.size());
        if(variables_.contains(p)) throw std::logic_error("Clue overlaps classic variable");
        std::set<Coord,ClassicCoordLess> actual;
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) {auto q=at(p,dx,dy); if(variables_.contains(q)) actual.insert(q);}
        if(actual!=r.neighbours || r.mines<0 || r.mines>static_cast<int>(actual.size())) throw std::logic_error("Unintended kernel variable adjacency at "+std::to_string(p.x)+","+std::to_string(p.y));
    }
    auto first=*variables_.begin(); int64_t minX=first.x,maxX=first.x,minY=first.y,maxY=first.y;
    auto extend=[&](Coord p) { minX=std::min(minX,p.x); maxX=std::max(maxX,p.x); minY=std::min(minY,p.y); maxY=std::max(maxY,p.y); };
    for(auto p:variables_) extend(p); for(const auto& [p,r]:counts_) extend(p);
    CellGrid g; g.origin=at({minX,minY},-padding,-padding);
    const int64_t w=maxX-minX+2*padding+1,h=maxY-minY+2*padding+1;
    if(w>32768 || h>32768 || w*h>256ll*1024*1024) throw std::invalid_argument("Board grid too large");
    g.width=static_cast<int>(w); g.height=static_cast<int>(h); g.cells.assign(static_cast<size_t>(w*h),9);
    auto index=[&](Coord p){return static_cast<size_t>((p.y-g.origin.y)*w+(p.x-g.origin.x));};
    for(auto p:variables_) g.cells[index(p)]=-1;
    for(const auto& [p,r]:counts_) g.cells[index(p)]=-2;
    std::vector<std::pair<size_t,int8_t>> digits; digits.reserve(counts_.size());
    for(const auto& [p,r]:counts_) {
        int fixedMines=0;
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) fixedMines+=g.cells[index(at(p,dx,dy))]==9;
        int n=fixedMines+r.mines; if(n<0 || n>8) throw std::logic_error("Invalid ordinary kernel clue");
        digits.push_back({index(p),static_cast<int8_t>(n)});
    }
    for(auto [i,d]:digits) g.cells[i]=d;
    return g;
}
Problem CellGrid::toProblem() const {
    Problem p; p.origin=origin; p.width=width; p.height=height;
    p.rules=RuleTable::classic(); p.boundary=Boundary::FixedHalo; p.haloSymbol=p.rules.index("*"); p.timeoutMs=10000;
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        int8_t c=cells[static_cast<size_t>(y)*width+x]; if(c<0) continue;
        p.pins.push_back({{origin.x+x,origin.y+y},c==9?std::string("*"):std::to_string(c)});
    }
    return p;
}
Circuit makeClassicKernelCircuit(CircuitKind kind) {
    ClassicPatch p;
    if(kind==CircuitKind::ClassicNor) { auto ports=p.nor({0,0}); return p.finish(kind,{ports[0],ports[1]},{ports[2]}); }
    if(kind==CircuitKind::ClassicCross) { auto ports=p.crossing({0,0}); return p.finish(kind,{ports[0],ports[1]},{ports[2],ports[3]}); }
    if(kind==CircuitKind::ClassicWireNotNor) {
        auto ports=p.nor({2,0});
        p.wire({{-14,2},{-6,2},{0,2}}); // Four wire inversions, then three for NOT.
        return p.finish(kind,{{"A",{-14,2}},ports[1]},{ports[2]});
    }
    if(kind==CircuitKind::ClassicRoutedNor) {
        auto cross=p.crossing({0,0}); auto gate=p.nor({16,0});
        p.wire({{0,2},{-4,2}}); p.wire({{6,2},{14,2}});
        // Two corner inversions replace one straight inversion to correct the vertical path phase.
        p.link({2,6},{3,7},{3,6}); p.link({3,7},{2,8},{3,8});
        p.wire({{2,8},{2,10},{14,10},{14,4}});
        return p.finish(kind,{cross[0],cross[1]},{gate[2],{"Z",{-4,2}}});
    }
    if(kind==CircuitKind::ClassicSR) {
        auto q=p.nor({0,0}),qb=p.nor({20,12});
        p.wire({{6,4},{12,4},{12,16},{18,16}});
        p.wire({{26,16},{30,16},{30,26},{-8,26},{-8,4},{-2,4}});
        return p.finish(kind,{{"S",qb[0].coord},{"R",q[0].coord}},{{"Q",q[2].coord},{"Qbar",qb[2].coord}});
    }
    if(kind==CircuitKind::ClassicSRStep) {
        auto h=p.nor({0,0}),q=p.nor({20,0}),qb=p.nor({0,16});
        p.wire({{6,4},{18,4}});
        p.wire({{-2,2},{-6,2},{-6,18},{-2,18}});
        p.wire({{26,4},{30,4},{30,30},{-10,30},{-10,20},{-2,20}});
        p.wire({{30,12},{24,12}});
        p.link({24,12},{23,11},{24,11}); p.link({23,11},{22,12},{22,11});
        p.wire({{22,12},{10,12},{10,20},{6,20}}); // Odd inversion count enforces Qbar = NOT Q.
        return p.finish(kind,{{"S",h[0].coord},{"R",q[0].coord},{"Qprev",h[1].coord}},{{"Q",q[2].coord},{"Qbar",qb[2].coord}});
    }
    throw std::invalid_argument("Unknown classic kernel circuit");
}
}
