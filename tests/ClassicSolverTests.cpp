#include "Constraints.h"
#include <iostream>
#include <stdexcept>
using namespace ims;
unsigned cases=0;
void require(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
void compare(Problem p) {
    auto compact=solve(p); p.compactClassic=false;
    for(bool propagate:{false,true}) {
        p.propagate=propagate; auto generic=solve(p); ++cases;
        require(compact.status==SolveStatus::Sat || compact.status==SolveStatus::Unsat,"compact solve failed");
        require(compact.status==generic.status,"compact/generic status differs");
        if(compact.status==SolveStatus::Sat) require(compact.modelChecked && generic.modelChecked,"unchecked model");
    }
}
int main() {
    try {
        for(int mines=0;mines<=8;++mines) {
            Problem base; base.width=base.height=1; base.rules=RuleTable::classic();
            int k=0; for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) base.pins.push_back({{dx,dy},k++<mines?"*":"0"});
            for(int symbol=-1;symbol<10;++symbol) {
                auto p=base; if(symbol>=0) p.pins.push_back({{0,0},base.rules.symbols[symbol]}); compare(p);
                auto r=solve(p); require(r.status==((symbol<0 || symbol==9 || symbol==mines)?SolveStatus::Sat:SolveStatus::Unsat),"ordinary independent count oracle differs");
            }
            for(int excluded=0;excluded<10;++excluded) { auto p=base; p.exclusions={{{0,0},"*"},{{0,0},p.rules.symbols[excluded]}}; compare(p); }
        }
        // Deterministic mixed pinned/excluded labels, fixed/free halo, arbitrary safe digits.
        unsigned seed=1729; auto random=[&](){seed=1664525u*seed+1013904223u; return seed;};
        for(int n=0;n<160;++n) {
            Problem p; p.width=3; p.height=2; p.origin={-1,-1}; p.rules=RuleTable::classic();
            p.boundary=n%2?Boundary::FixedHalo:Boundary::FreeHalo; p.haloSymbol=n%10;
            Layout l; require(makeLayout(p,l).empty(),"mixed layout invalid");
            for(int j=0;j<9;++j) {
                auto coord=l.coord(static_cast<int>(random()%l.width),static_cast<int>(random()%l.height));
                if(j%2) p.pins.push_back({coord,p.rules.symbols[random()%10]});
                else p.exclusions.push_back({coord,p.rules.symbols[random()%10]});
            }
            compare(p);
        }
        for(Coord at:{Coord{0,0},Coord{-1,-1}}) {
            Problem p; p.width=p.height=1; p.rules=RuleTable::classic();
            for(const auto& s:p.rules.symbols) p.exclusions.push_back({at,s}); compare(p);
        }
        Problem p; p.rules=RuleTable::classic(); p.width=p.height=2048; require(solve(p).status==SolveStatus::Invalid,"ordinary area cap ignored");
        p.width=32769; p.height=1; require(solve(p).status==SolveStatus::Invalid,"ordinary axis cap ignored");
        p.width=300; require(solve(p).status==SolveStatus::Sat,"larger ordinary region rejected");
        std::cout<<"PASS: "<<cases<<" compact/generic comparisons, independent counts, exclusions, halos, resource caps"<<std::endl;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<std::endl; return 1;}
}
