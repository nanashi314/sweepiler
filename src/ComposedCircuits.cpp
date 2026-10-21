#include "ComposedCircuits.h"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
namespace ims {
namespace {
struct Less { bool operator()(Coord a,Coord b) const { return a.y<b.y || (a.y==b.y && a.x<b.x); } };
using Cells=std::map<Coord,std::string,Less>;
using Points=std::set<Coord,Less>;
Coord at(Coord a,int x,int y) { return {a.x+x,a.y+y}; }
struct Patch {
    Points binary,unknown;
    Cells clues,overrides;
    std::vector<Circuit> crosses;
    std::vector<CircuitPart> parts;
    Circuit gate(CircuitKind kind,Coord offset,int turns=0) {
        auto c=makeCircuit(kind);
        for(int t=0;t<turns;++t) c=rotated(c);
        c=translated(c,offset);
        parts.push_back({std::string(circuitName(kind))+" #"+std::to_string(parts.size()+1),
            {{c.problem.origin.x+2,c.problem.origin.y+2},{c.problem.origin.x+c.problem.width-3,c.problem.origin.y+c.problem.height-3}}});
        for(auto p:c.signals) binary.insert(p);
        for(const auto& pin:c.problem.pins) {
            auto tag=pin.symbol.substr(0,pin.symbol.find('_'));
            if(tag=="W" || tag=="G" || tag=="NOT" || tag=="I") clue(pin.coord,tag);
        }
        return c;
    }
    void clue(Coord p,const std::string& tag) {
        if((p.x+p.y)%2==0) throw std::logic_error("Composed clue parity");
        auto [it,fresh]=clues.emplace(p,tag);
        if(!fresh && it->second!=tag) throw std::logic_error("Conflicting composed clues");
    }
    void wire(std::initializer_list<Coord> corners) {
        std::optional<Coord> last;
        for(auto p:corners) {
            binary.insert(p);
            if(last) {
                auto q=*last;
                if((q.x!=p.x && q.y!=p.y) || (std::abs(q.x-p.x)+std::abs(q.y-p.y))%2) throw std::logic_error("Wire needs axis-aligned even steps");
                int dx=p.x==q.x?0:(p.x>q.x?2:-2),dy=p.y==q.y?0:(p.y>q.y?2:-2);
                while(q!=p) { clue(at(q,dx/2,dy/2),"W"); q=at(q,dx,dy); binary.insert(q); }
            }
            last=p;
        }
    }
    void shift(Coord a,Coord b,Coord w) {
        binary.insert(a); binary.insert(b); clue(w,"W");
    }
    void phaseLink(Coord a) {
        // Two corner W clues replace one straight W, retaining rather than flipping the bit.
        shift(a,at(a,1,1),at(a,0,1));
        shift(at(a,1,1),at(a,2,0),at(a,2,1));
    }
    void crossing(Coord center) {
        if((center.x+center.y)%2==0) throw std::logic_error("Crossing center parity");
        auto c=translated(makeCircuit(CircuitKind::Crossover),center);
        parts.push_back({"CROSSOVER",{at(center,-3,-3),at(center,3,3)}});
        const auto prefix="X"+std::to_string(crosses.size())+"_";
        for(auto& pin:c.problem.pins) if(pin.symbol!="N") pin.symbol=prefix+pin.symbol;
        for(auto& s:c.problem.rules.symbols) if(s!="0" && s!="1" && s!="N") s=prefix+s;
        for(const auto& pin:c.problem.pins) if(std::abs(pin.coord.x-center.x)<=3 && std::abs(pin.coord.y-center.y)<=3) overrides[pin.coord]=pin.symbol;
        for(auto p:c.signals) unknown.insert(p);
        for(const auto& port:c.inputs) binary.insert(port.coord);
        for(const auto& port:c.outputs) binary.insert(port.coord);
        crosses.push_back(std::move(c));
    }
    Circuit finish(CircuitKind kind,std::vector<Port> inputs,std::vector<Port> outputs) const {
        Circuit c; c.kind=kind; c.inputs=std::move(inputs); c.outputs=std::move(outputs); c.parts=parts;
        Points holes=unknown; holes.insert(binary.begin(),binary.end());
        if(holes.empty()) throw std::logic_error("Empty composition");
        int64_t minX=holes.begin()->x,maxX=minX,minY=holes.begin()->y,maxY=minY;
        for(auto p:holes) { minX=std::min(minX,p.x); maxX=std::max(maxX,p.x); minY=std::min(minY,p.y); maxY=std::max(maxY,p.y); }
        c.problem.origin={minX-2,minY-2}; c.problem.width=static_cast<int>(maxX-minX+5); c.problem.height=static_cast<int>(maxY-minY+5); c.problem.timeoutMs=10000;
        Layout l; if(auto e=makeLayout(c.problem,l);!e.empty()) throw std::logic_error(e);
        Cells fixed;
        for(int y=0;y<l.height;++y) for(int x=0;x<l.width;++x) {
            auto p=l.coord(x,y); if(holes.contains(p)) continue;
            auto o=overrides.find(p);
            fixed[p]=o!=overrides.end()?o->second:((p.x+p.y)%2!=0?"@":"N");
        }
        for(auto p:holes) if(overrides.contains(p)) throw std::logic_error("Crossing overlaps signal");
        auto nFixed=[&](Coord p) { int n=0; for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) { auto it=fixed.find(at(p,dx,dy)); n+=it!=fixed.end() && it->second=="N"; } return n; };
        std::map<std::string,std::pair<std::string,int>> specs;
        for(auto& [p,s]:fixed) if(s=="@") {
            auto it=clues.find(p); auto tag=it==clues.end()?"PAD":it->second;
            int n=nFixed(p); s=tag+"_"+std::to_string(n); specs[s]={tag,n};
        }
        for(const auto& [p,tag]:clues) if(!fixed.contains(p) || fixed.at(p)!=tag+"_"+std::to_string(nFixed(p))) throw std::logic_error("Route overlaps clue or crossover");
        Points covered;
        for(const auto& [p,tag]:clues) {
            int arity=0;
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) {
                auto q=at(p,dx,dy); if(!holes.contains(q)) continue;
                if(!binary.contains(q)) throw std::logic_error("Paper clue touches a typed crossing interior");
                ++arity; covered.insert(q);
            }
            if(arity!=(tag=="W"?2:tag=="I"?3:4)) throw std::logic_error("Unexpected wire/gate adjacency");
        }
        for(auto p:binary) if(!covered.contains(p)) throw std::logic_error("Binary signal lacks a count constraint");
        auto& r=c.problem.rules; r.symbols={"0","1","N"};
        for(const auto& [s,spec]:specs) r.symbols.push_back(s);
        for(const auto& x:crosses) for(size_t i=3;i<x.problem.rules.symbols.size();++i) r.symbols.push_back(x.problem.rules.symbols[i]);
        r.counts.assign(r.symbols.size(),std::vector<int>(r.symbols.size(),-1));
        for(const auto& [s,spec]:specs) {
            auto& row=r.counts[r.index(s)]; row[2]=spec.second;
            if(spec.first=="W") row[0]=row[1]=1;
            else if(spec.first=="NOT" || spec.first=="G") row[0]=row[1]=2;
            else if(spec.first=="I") { row[0]=2; row[1]=1; }
        }
        for(const auto& x:crosses) {
            const auto& t=x.problem.rules;
            for(size_t i=3;i<t.symbols.size();++i) {
                auto& row=r.counts[r.index(t.symbols[i])]; std::fill(row.begin(),row.end(),0);
                for(size_t j=0;j<t.symbols.size();++j) row[r.index(t.symbols[j])]=t.counts[i][j];
            }
        }
        for(const auto& [p,s]:fixed) c.problem.pins.push_back({p,s});
        c.signals.assign(holes.begin(),holes.end());
        c.provenance="Prescribed Kaye gate patches with physical W routes and shared pinned background; custom crossing where marked";
        return c;
    }
};
}
Circuit makeComposedCircuit(CircuitKind kind) {
    Patch p;
    if(kind==CircuitKind::WireNotNor) {
        p.gate(CircuitKind::Wire,{0,0}); p.gate(CircuitKind::Not,{12,-2}); p.gate(CircuitKind::Nor,{28,0});
        p.wire({{8,0},{12,0}}); p.wire({{22,0},{28,0}});
        return p.finish(kind,{{"A",{0,0}},{"B",{28,6}}},{{"Y",{46,2}}});
    }
    if(kind==CircuitKind::RoutedNor) {
        p.gate(CircuitKind::Wire,{0,0});
        p.crossing({11,0});
        p.gate(CircuitKind::Nor,{22,0});
        p.wire({{4,0},{4,-4}}); // Physical fanout, same phase.
        p.wire({{14,0},{22,0}});
        p.wire({{11,-7},{11,-3}});
        p.wire({{11,3},{11,5}});
        p.shift({11,5},{10,6},{10,5});
        p.wire({{10,6},{8,6},{8,8},{22,8},{22,6}});
        return p.finish(kind,{{"A",{0,0}},{"B",{11,-7}}},{{"Y",{40,2}},{"Z",{4,-4}}});
    }
    if(kind==CircuitKind::NorSR) {
        p.gate(CircuitKind::Nor,{0,0});
        p.gate(CircuitKind::Nor,{30,16});
        // Both feedback paths consist entirely of W cells. No solver equalities.
        p.wire({{18,2},{24,2},{24,22},{30,22}});
        p.wire({{48,18},{50,18},{50,30},{-4,30},{-4,6},{0,6}});
        return p.finish(kind,{{"S",{30,16}},{"R",{0,0}}},{{"Q",{18,2}},{"Qbar",{48,18}}});
    }
    if(kind==CircuitKind::NorSRStep) {
        p.gate(CircuitKind::Nor,{0,0});   // H = NOR(S,Qprev)
        p.gate(CircuitKind::Nor,{30,0});  // Q = NOR(R,H)
        p.gate(CircuitKind::Nor,{0,20});  // Qbar = NOR(S,Q)
        p.gate(CircuitKind::Not,{32,16}); // Qbar = NOT(Q); excludes S=R=1.
        p.wire({{18,2},{24,2},{24,6},{30,6}});
        p.wire({{0,0},{-4,0},{-4,20},{0,20}});
        p.wire({{48,2},{50,2},{50,32},{-6,32},{-6,26},{0,26}});
        p.wire({{50,14},{28,14},{28,18},{32,18}});
        p.wire({{18,22},{20,22}}); p.phaseLink({20,22});
        p.wire({{22,22},{40,22},{40,18}});
        return p.finish(kind,{{"S",{0,0}},{"R",{30,0}},{"Qprev",{0,6}}},{{"Q",{48,2}},{"Qbar",{18,22}}});
    }
    throw std::invalid_argument("Unknown composed circuit");
}
}
