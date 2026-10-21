#include "Circuits.h"
#include "ComposedCircuits.h"
#include "ClassicCircuits.h"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
namespace ims {
namespace {
struct Less { bool operator()(Coord a,Coord b) const { return a.y<b.y || (a.y==b.y && a.x<b.x); } };
using Cells=std::map<Coord,std::string,Less>;
using Points=std::set<Coord,Less>;
Coord plus(Coord a,Coord b) {
    Coord r; if(!addChecked(a.x,b.x,r.x) || !addChecked(a.y,b.y,r.y)) throw std::invalid_argument("Circuit coordinate overflow"); return r;
}
constexpr Coord directions[]={{-1,0},{1,0},{0,-1},{0,1}};
struct PaperPatch {
    Cells suffix;
    Points signals;
    void bit(int x,int y) { signals.insert({x,y}); }
    void clue(int x,int y,std::string s) {
        if((x+y)%2==0) throw std::logic_error("Scaffold parity error");
        auto [it,fresh]=suffix.emplace(Coord{x,y},s);
        if(!fresh && it->second!=s) throw std::logic_error("Overlapping scaffold clues");
    }
    void wire(std::initializer_list<Coord> path) {
        std::optional<Coord> previous;
        for(auto p:path) {
            signals.insert(p);
            if(previous) {
                auto dx=p.x-previous->x,dy=p.y-previous->y;
                if(!((std::abs(dx)==2 && dy==0) || (std::abs(dy)==2 && dx==0))) throw std::logic_error("Invalid wire step");
                clue(static_cast<int>((p.x+previous->x)/2),static_cast<int>((p.y+previous->y)/2),"W");
            }
            previous=p;
        }
    }
    Circuit finish(CircuitKind kind,int maxX,int maxY) const {
        Circuit c; c.kind=kind; c.problem.origin={-2,-2}; c.problem.width=maxX+5; c.problem.height=maxY+5;
        c.problem.timeoutMs=10000; c.problem.rules.symbols={"0","1","N"};
        Cells scaffold; Layout l;
        // All checkerboard background cells get distinct, visible Delta identities.
        for(int y=-3;y<=maxY+3;++y) for(int x=-3;x<=maxX+3;++x) if((x+y)%2!=0) {
            auto it=suffix.find({x,y}); int active=0;
            for(auto d:directions) active+=signals.contains(plus({x,y},d));
            auto tag=it==suffix.end()?std::to_string(4-active):it->second;
            auto label=tag+"_"+std::to_string(scaffold.size());
            scaffold[{x,y}]=label; c.problem.rules.symbols.push_back(label);
        }
        auto& table=c.problem.rules;
        table.counts.assign(table.symbols.size(),std::vector<int>(table.symbols.size(),-1));
        for(const auto& [p,label]:scaffold) {
            auto index=table.index(label); auto& row=table.counts[index];
            // Four diagonal Delta neighbours: exact identity counts, not a hidden domain filter.
            for(size_t i=3;i<row.size();++i) row[i]=0;
            for(int dy:{-1,1}) for(int dx:{-1,1}) {
                auto it=scaffold.find(plus(p,{dx,dy})); if(it!=scaffold.end()) ++row[table.index(it->second)];
            }
            auto tag=label.substr(0,label.find('_'));
            if(tag=="W") { row[0]=1; row[1]=1; row[2]=2; }
            else if(tag=="NOT" || tag=="G") { row[0]=2; row[1]=2; row[2]=0; }
            else if(tag=="I") { row[0]=2; row[1]=1; row[2]=1; }
            else row[2]=std::stoi(tag);
        }
        if(auto error=makeLayout(c.problem,l);!error.empty()) throw std::logic_error(error);
        for(int y=0;y<l.height;++y) for(int x=0;x<l.width;++x) {
            auto p=l.coord(x,y); auto it=scaffold.find(p);
            if(it!=scaffold.end()) c.problem.pins.push_back({p,it->second});
            else if(!signals.contains(p)) c.problem.pins.push_back({p,"N"});
        }
        c.signals.assign(signals.begin(),signals.end());
        return c;
    }
};
Circuit paperCircuit(CircuitKind kind) {
    PaperPatch p; Circuit c;
    if(kind==CircuitKind::Wire) {
        p.wire({{0,0},{2,0},{4,0},{6,0},{8,0}}); c=p.finish(kind,8,0);
        c.inputs={{"A",{0,0}}}; c.outputs={{"Y",{8,0}}};
        c.provenance="Kaye p.8 WIRE; finite pinned scaffold and neutral padding";
    } else if(kind==CircuitKind::Not) {
        p.wire({{0,2},{2,2},{4,2}}); p.wire({{6,2},{8,2},{10,2}});
        p.bit(5,1); p.bit(5,3); p.clue(5,2,"NOT");
        for(int y:{1,3}) { p.clue(2,y,"3"); p.clue(4,y,"W"); p.clue(6,y,"W"); p.clue(8,y,"3"); }
        for(int y:{0,4}) { p.clue(3,y,"4"); p.clue(5,y,"3"); p.clue(7,y,"4"); }
        c=p.finish(kind,10,4); c.inputs={{"A",{0,2}}}; c.outputs={{"Y",{8,2}}};
        c.provenance="Kaye p.10 NOT; output tap at (8,2) fixes alternating-wire phase";
    } else if(kind==CircuitKind::Nor) {
        // Coordinates transcribed from the numerical example on p.12, not evaluated bits.
        const std::vector<std::vector<std::pair<int,std::string>>> rows={
            {{0,"s"},{1,"W"},{2,"s"},{3,"W"},{4,"s"},{5,"W"},{6,"s"}},
            {{4,"3"},{6,"W"}},
            {{3,"3"},{5,"2"},{6,"s"},{7,"2"},{9,"3"},{11,"W"},{12,"s"},{13,"W"},{14,"s"},{15,"W"},{16,"s"},{17,"W"},{18,"s"}},
            {{2,"W"},{3,"s"},{4,"W"},{5,"s"},{6,"G"},{7,"s"},{8,"W"},{9,"s"},{10,"W"},{11,"s"},{12,"W"},{14,"W"}},
            {{2,"s"},{3,"W"},{5,"2"},{6,"s"},{7,"2"},{9,"3"},{11,"2"},{13,"3"},{14,"s"}},
            {{2,"W"},{4,"4"},{6,"W"},{8,"3"},{10,"W"},{11,"s"},{12,"W"},{14,"W"}},
            {{0,"s"},{1,"W"},{2,"s"},{5,"3"},{6,"s"},{7,"I"},{8,"s"},{9,"W"},{10,"s"},{11,"NOT"},{12,"s"},{13,"W"},{14,"s"}},
            {{6,"2"},{7,"s"},{8,"2"},{10,"W"},{11,"s"},{12,"W"}},
            {{7,"3"}}
        };
        for(int y=0;y<static_cast<int>(rows.size());++y) for(const auto& [x,s]:rows[y]) {
            if(s=="s") p.bit(x,y); else p.clue(x,y,s);
        }
        c=p.finish(kind,18,8); c.inputs={{"A",{0,0}},{"B",{0,6}}}; c.outputs={{"Y",{18,2}}};
        c.provenance="Kaye p.12 numerical NOR example: (3,2) uses suffix 3 (symbolic diagram prints 2)";
    } else if(kind==CircuitKind::Fanout) {
        p.wire({{0,0},{2,0},{4,0},{6,0},{8,0}}); p.wire({{4,0},{4,2},{4,4}});
        c=p.finish(kind,8,4); c.inputs={{"A",{0,0}}}; c.outputs={{"Y",{8,0}},{"Z",{4,4}}};
        c.provenance="Independent finite fanout using Kaye W rules; not a literal p.10 transcription";
    } else {
        p.wire({{0,0},{2,0},{4,0},{4,2},{4,4}}); c=p.finish(kind,4,4);
        c.inputs={{"A",{0,0}}}; c.outputs={{"Y",{4,4}}};
        c.provenance="Independent finite 90-degree bend using Kaye W rules";
    }
    return c;
}
Circuit crossing() {
    // Independent finite gadget. Public 0/1/N rows stay entirely unconstrained.
    // A center P_ab transmits two bits through typed arms and local count adapters.
    Circuit c; c.kind=CircuitKind::Crossover;
    c.problem.origin={-4,-4}; c.problem.width=c.problem.height=9; c.problem.timeoutMs=10000;
    auto& t=c.problem.rules; t.symbols={"0","1","N","KNW","KNE","KSE","KSW"};
    Cells fixed; Points unknown;
    for(int y=-5;y<=5;++y) for(int x=-5;x<=5;++x) fixed[{x,y}]="N";
    const Coord inner[]={{-1,0},{1,0},{0,-1},{0,1}};
    const Coord bridge[]={{-2,0},{2,0},{0,-2},{0,2}};
    const Coord ports[]={{-3,0},{3,0},{0,-3},{0,3}};
    const Coord corners[]={{-1,-1},{1,-1},{1,1},{-1,1}};
    const char* names[]={"L","R","T","B"};
    for(int i=0;i<4;++i) fixed[corners[i]]=t.symbols[3+i];
    std::array<std::array<std::string,2>,4> armNames,bridgeNames,guardNames;
    for(int i=0;i<4;++i) {
        for(int bit=0;bit<2;++bit) {
            armNames[i][bit]=std::string(names[i])+std::to_string(bit);
            bridgeNames[i][bit]="E"+armNames[i][bit];
            guardNames[i][bit]="M"+std::string(names[i])+std::to_string(bit);
            t.symbols.push_back(armNames[i][bit]); t.symbols.push_back(bridgeNames[i][bit]); t.symbols.push_back(guardNames[i][bit]);
            auto g=bridge[i]; if(i<2) g.y=bit?1:-1; else g.x=bit?1:-1;
            fixed[g]=guardNames[i][bit];
        }
        for(auto p:{inner[i],bridge[i],ports[i]}) { fixed.erase(p); unknown.insert(p); }
    }
    for(int a=0;a<2;++a) for(int b=0;b<2;++b) t.symbols.push_back("P"+std::to_string(a)+std::to_string(b));
    fixed.erase(Coord{0,0}); unknown.insert({0,0});
    t.counts.assign(t.symbols.size(),std::vector<int>(t.symbols.size(),-1));
    auto rowAt=[&](const std::string& label,Coord p)->std::vector<int>& {
        auto& row=t.counts[t.index(label)]; std::fill(row.begin(),row.end(),0);
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) {
            auto it=fixed.find(plus(p,{dx,dy})); if(it!=fixed.end()) ++row[t.index(it->second)];
        }
        return row;
    };
    auto allow=[&](std::vector<int>& row,const std::string& s) { row[t.index(s)]=-1; };
    auto allowPair=[&](std::vector<int>& row,const auto& pair) { for(const auto& s:pair) allow(row,s); };
    for(int corner=0;corner<4;++corner) {
        auto& row=rowAt(t.symbols[3+corner],corners[corner]);
        // All typed symbols may occur here; their own marker-count rows locate them.
        for(int i=0;i<4;++i) { allowPair(row,armNames[i]); allowPair(row,bridgeNames[i]); }
        for(int a=0;a<2;++a) for(int b=0;b<2;++b) allow(row,"P"+std::to_string(a)+std::to_string(b));
    }
    for(int i=0;i<4;++i) for(int bit=0;bit<2;++bit) {
        auto guard=bridge[i]; if(i<2) guard.y=bit?1:-1; else guard.x=bit?1:-1;
        auto& grow=rowAt(guardNames[i][bit],guard);
        allow(grow,"0"); allow(grow,"1"); allowPair(grow,armNames[i]); allowPair(grow,bridgeNames[i]);
        auto& row=rowAt(armNames[i][bit],inner[i]);
        for(int other=0;other<4;++other) if((i<2)!=(other<2)) allowPair(row,armNames[other]);
        for(int a=0;a<2;++a) for(int b=0;b<2;++b) allow(row,"P"+std::to_string(a)+std::to_string(b));
        row[t.index(bridgeNames[i][bit])]=1;
        auto& erow=rowAt(bridgeNames[i][bit],bridge[i]);
        allowPair(erow,armNames[i]); erow[bit]=1; // Exactly one adjacent public binary symbol of this bit.
    }
    for(int a=0;a<2;++a) for(int b=0;b<2;++b) {
        auto& row=rowAt("P"+std::to_string(a)+std::to_string(b),{0,0});
        for(int i=0;i<4;++i) row[t.index(armNames[i][i<2?a:b])]=1;
    }
    for(const auto& [p,s]:fixed) c.problem.pins.push_back({p,s});
    c.signals.assign(unknown.begin(),unknown.end());
    c.inputs={{"A",ports[0]},{"B",ports[2]}}; c.outputs={{"Y",ports[1]},{"Z",ports[3]}};
    c.provenance="Independent crossover with typed arms, two-bit center and count-only 0/1 adapters; not in Kaye";
    return c;
}
}
const char* circuitName(CircuitKind k) {
    switch(k) {
    case CircuitKind::Wire:return "WIRE";case CircuitKind::Not:return "NOT";case CircuitKind::Nor:return "NOR";
    case CircuitKind::Fanout:return "FANOUT";case CircuitKind::Bend:return "BEND";case CircuitKind::Crossover:return "CROSSOVER";
    case CircuitKind::WireNotNor:return "WIRE-NOT-NOR";case CircuitKind::RoutedNor:return "ROUTED NOR";case CircuitKind::NorSR:return "NOR SR LATCH";case CircuitKind::NorSRStep:return "NOR SR STEP";
    case CircuitKind::ClassicWire:return "CLASSIC WIRE";case CircuitKind::ClassicNot:return "CLASSIC INVERT";
    case CircuitKind::ClassicBend:return "CLASSIC BEND";case CircuitKind::ClassicFanout:return "CLASSIC FANOUT";
    case CircuitKind::ClassicNor:return "CLASSIC NOR";case CircuitKind::ClassicCross:return "CLASSIC CROSSOVER";
    case CircuitKind::ClassicWireNotNor:return "CLASSIC WIRE-NOT-NOR";case CircuitKind::ClassicRoutedNor:return "CLASSIC ROUTED NOR";
    case CircuitKind::ClassicSR:return "CLASSIC SR LATCH";default:return "CLASSIC SR STEP";
    }
}
Circuit makeCircuit(CircuitKind k) {
    auto c=isClassicCircuit(k)?makeClassicCircuit(k):(k>=CircuitKind::WireNotNor?makeComposedCircuit(k):(k==CircuitKind::Crossover?crossing():paperCircuit(k)));
    Layout l; if(auto error=makeLayout(c.problem,l);!error.empty()) throw std::logic_error(error);
    for(const auto& port:c.inputs) if(!l.contains(port.coord)) throw std::logic_error("Input outside fixture");
    for(const auto& port:c.outputs) if(!l.contains(port.coord)) throw std::logic_error("Output outside fixture");
    return c;
}
void pinPort(Problem& p,const Port& port,int bit) {
    if(bit<0 || bit>1) throw std::invalid_argument("Port bit must be 0 or 1");
    std::erase_if(p.pins,[&](const Pin& pin){return pin.coord==port.coord;}); p.pins.push_back({port.coord,std::to_string(bit)});
}
Circuit translated(Circuit c,Coord offset) {
    c.problem.origin=plus(c.problem.origin,offset);
    for(auto& pin:c.problem.pins) pin.coord=plus(pin.coord,offset);
    for(auto& e:c.problem.exclusions) e.coord=plus(e.coord,offset);
    for(auto& p:c.inputs) p.coord=plus(p.coord,offset);
    for(auto& p:c.outputs) p.coord=plus(p.coord,offset);
    for(auto& p:c.signals) p=plus(p,offset);
    for(auto& part:c.parts) { part.bounds.min=plus(part.bounds.min,offset); part.bounds.max=plus(part.bounds.max,offset); }
    Layout l; if(auto error=makeLayout(c.problem,l);!error.empty()) throw std::invalid_argument(error);
    return c;
}
Circuit rotated(Circuit c) {
    auto rotate=[](Coord p) { if(p.y==INT64_MIN) throw std::invalid_argument("Rotation overflow"); return Coord{-p.y,p.x}; };
    Coord last=plus(c.problem.origin,{c.problem.width-1,c.problem.height-1});
    auto first=rotate(c.problem.origin); auto second=rotate(last);
    c.problem.origin={std::min(first.x,second.x),std::min(first.y,second.y)}; std::swap(c.problem.width,c.problem.height);
    for(auto& pin:c.problem.pins) pin.coord=rotate(pin.coord);
    for(auto& e:c.problem.exclusions) e.coord=rotate(e.coord);
    for(auto& p:c.inputs) p.coord=rotate(p.coord);
    for(auto& p:c.outputs) p.coord=rotate(p.coord);
    for(auto& p:c.signals) p=rotate(p);
    for(auto& part:c.parts) {
        auto a=rotate(part.bounds.min),b=rotate(part.bounds.max);
        part.bounds={{std::min(a.x,b.x),std::min(a.y,b.y)},{std::max(a.x,b.x),std::max(a.y,b.y)}};
    }
    return c;
}
}
