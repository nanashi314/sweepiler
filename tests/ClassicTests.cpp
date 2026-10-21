#include "ClassicCircuits.h"
#include <iostream>
#include <stdexcept>
using namespace ims;
namespace {
unsigned cases=0;
void require(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
void ordinaryModel(const Problem& p,const SolveResult& r) {
    // Independent ordinary rule check. Outside the finite layout is an all-safe zero extension.
    auto mine=[&](Coord c) { return r.layout.contains(c)?p.rules.symbols[r.labels[r.layout.index(c)]]=="*":p.boundary==Boundary::FixedHalo && p.haloSymbol==p.rules.index("*"); };
    for(int y=0;y<r.layout.height;++y) for(int x=0;x<r.layout.width;++x) {
        auto pos=r.layout.coord(x,y); auto s=p.rules.symbols[r.labels[r.layout.index(pos)]];
        require(s=="*" || (s.size()==1 && s[0]>='0' && s[0]<='8'),"Extended symbol in ordinary model");
        if(s=="*") continue;
        int n=0; for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) n+=mine({pos.x+dx,pos.y+dy});
        require(n==s[0]-'0',"Ordinary mine count mismatch, including halo");
    }
}
SolveResult expect(const Problem& p,SolveStatus wanted) {
    ++cases; auto r=solve(p);
    if(r.status!=wanted) std::cerr<<statusName(r.status)<<": "<<r.message<<'\n';
    require(r.status==wanted,"Classic SAT mismatch");
    if(wanted==SolveStatus::Sat) { require(r.modelChecked,"Unchecked generic model"); ordinaryModel(p,r); }
    return r;
}
void table(const Circuit& c) {
    auto report=verifyCircuit(c); cases+=static_cast<unsigned>(report.checks.size());
    for(const auto& row:report.checks) if(row.actual!=(row.expectedSat?SolveStatus::Sat:SolveStatus::Unsat)) std::cerr<<circuitName(c.kind)<<" in="<<row.inputs<<" out="<<row.outputs<<" "<<statusName(row.actual)<<": "<<row.detail<<'\n';
    require(report.passed,"Classic truth table failed");
}
}
int main() {
    try {
        for(auto kind:classicKinds) {
            auto c=makeCircuit(kind);
            require(c.problem.rules.symbols==RuleTable::classic().symbols && c.problem.rules.counts==RuleTable::classic().counts,"Modified ordinary rule table");
            for(auto pos:c.signals) for(const auto& pin:c.problem.pins) require(pin.coord!=pos,"Prefilled signal");
            for(const auto& pin:c.problem.pins) require(classicBit(pin.symbol)>=0,"Nonstandard scaffold");
            for(bool propagation:{true,false}) {
                c.problem.propagate=propagation; c.problem.compactClassic=propagation; table(c);
                // Independent Boolean enumeration: each revealed 1 demands exactly one neighbouring mine.
                unsigned accepted=0;
                if(c.signals.size()<=10) {
                for(unsigned mask=0;mask<(1u<<c.signals.size());++mask) {
                    auto p=c.problem; bool valid=true;
                    for(size_t i=0;i<c.signals.size();++i) {
                        pinClassicPort(p,{"signal",c.signals[i]},(mask>>i)&1);
                    }
                    Layout l; require(makeLayout(p,l).empty(),"Invalid oracle layout");
                    auto mine=[&](Coord q) {
                        for(size_t i=0;i<c.signals.size();++i) if(c.signals[i]==q) return ((mask>>i)&1)!=0;
                        for(const auto& pin:c.problem.pins) if(pin.coord==q) return pin.symbol=="*";
                        int x=static_cast<int>(q.x-l.min.x),y=static_cast<int>(q.y-l.min.y);
                        return !l.core(x,y) && p.boundary==Boundary::FixedHalo && p.haloSymbol==p.rules.index("*");
                    };
                    for(const auto& pin:c.problem.pins) if(pin.symbol!="*") {
                        int n=0;
                        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) n+=mine({pin.coord.x+dx,pin.coord.y+dy});
                        valid&=n==pin.symbol[0]-'0';
                    }
                    expect(p,valid?SolveStatus::Sat:SolveStatus::Unsat); accepted+=valid;
                }
                require(accepted==(c.inputs.size()==2?4u:2u),"Unexpected number of full mine assignments");
                }
                for(unsigned bits=0;bits<(1u<<c.inputs.size());++bits) {
                    auto p=c.problem; for(size_t i=0;i<c.inputs.size();++i) pinClassicPort(p,c.inputs[i],(bits>>i)&1);
                    bool legal=kind!=CircuitKind::ClassicSRStep || (bits&3)!=3;
                    auto r=expect(p,legal?SolveStatus::Sat:SolveStatus::Unsat); if(!legal) continue;
                    if(kind==CircuitKind::ClassicNor && bits==0) {
                        auto s=p.rules.symbols[r.labels[r.layout.index(c.inputs[0].coord)]];
                        require(s!="0" && s!="*","NOR safety was incorrectly encoded as a zero clue");
                    }
                    unsigned actual=0;
                    for(size_t i=0;i<c.outputs.size();++i)
                        actual|=static_cast<unsigned>(classicBit(p.rules.symbols[r.labels[r.layout.index(c.outputs[i].coord)]]))<<i;
                    require(referenceAccepts(kind,bits,actual),"Wrong unpinned classic output");
                }
            }
            c.problem.propagate=true; table(rotated(c));
            // Keep arithmetic in the independent checker away from int64 endpoints.
            table(translated(c,{1000000000000000000LL,-1000000000000000000LL}));
            auto broken=c.problem; broken.propagate=false;
            Coord cut=kind==CircuitKind::ClassicNor?Coord{2,3}:(kind==CircuitKind::ClassicCross || kind==CircuitKind::ClassicRoutedNor)?Coord{2,2}:kind==CircuitKind::ClassicWireNotNor?Coord{-13,2}:kind==CircuitKind::ClassicSR?Coord{27,16}:kind==CircuitKind::ClassicSRStep?Coord{-1,4}:Coord{1,0};
            std::erase_if(broken.pins,[&](const Pin& p){return p.coord==cut;});
            unsigned inputBits=kind==CircuitKind::ClassicSR?1:0;
            for(size_t i=0;i<c.inputs.size();++i) pinClassicPort(broken,c.inputs[i],(inputBits>>i)&1);
            pinClassicPort(broken,c.outputs[0],1-static_cast<int>(referenceOutput(kind,inputBits)&1));
            expect(broken,SolveStatus::Sat); // Removing an actual clue breaks the signal relation.
            std::cout<<circuitName(kind)<<": ordinary rules, truth tables and wrong-output probes PASS\n";
        }
        std::cout<<"PASS: "<<cases<<" classic solves; digits and mines only\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
