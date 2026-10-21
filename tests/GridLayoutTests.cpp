#include "GridLayout.h"
#include "ClassicCircuits.h"
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace ims;
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
size_t checks{};
struct Net {int net;Coord at;};
// Random NOR DAGs placed row-major in slots; inputs on the left edge and
// outputs on the right edge. The reference evaluation is used only here.
int pitchX=40,pitchY=32,window=12;
void randomCase(unsigned seed,int inputs,int gates,int columns,int outputs) {
    std::mt19937 rng(seed); GridLayout g;
    struct Gate {int a,b;bool ia,ib;int net;Coord out;};
    std::vector<int> nets; std::vector<Coord> inputCells; std::vector<Gate> list;
    for(int i=0;i<inputs;++i) {Coord c{0,4+8*i}; inputCells.push_back(c); nets.push_back(g.input(c,GridLayout::Side::Right));}
    for(int i=0;i<gates;++i) {
        int span=static_cast<int>(nets.size());
        int a=std::uniform_int_distribution<int>(std::max(0,span-8),span-1)(rng),b=std::uniform_int_distribution<int>(std::max(0,span-window),span-1)(rng);
        bool ia=rng()%3==0,ib=rng()%4==0;
        Coord slot{24+pitchX*(i%columns),2+pitchY*(i/columns)};
        int net=g.nor({nets[a],ia},{nets[b],ib},slot);
        list.push_back({a,b,ia,ib,net,{slot.x+6,slot.y+4}}); nets.push_back(net);
    }
    int right=24+pitchX*columns+16;
    std::vector<Coord> outCells;
    for(int o=0;o<outputs;++o) {Coord port{right,4+8*o}; g.output({nets[nets.size()-1-o],o%2==1},port,GridLayout::Side::Right); outCells.push_back(port);}
    GridInfo info; auto p=g.compile(&info);
    std::cout<<"seed "<<seed<<": "<<gates<<" gates "<<p.width<<'x'<<p.height<<", crossings "<<info.crossings<<", adapters "<<info.adapters<<", retries "<<info.retries<<", "<<info.routeMs<<" ms"<<std::endl;
    for(int mask=0;mask<(1<<inputs);++mask) {
        auto base=p; std::vector<int> values;
        for(int i=0;i<inputs;++i) {values.push_back((mask>>i)&1); pinClassicPort(base,{"in",inputCells[i]},values.back());}
        for(const auto& gate:list) values.push_back(!((values[gate.a]^static_cast<int>(gate.ia))||(values[gate.b]^static_cast<int>(gate.ib))));
        auto r=solve(base); ++checks; require(r.status==SolveStatus::Sat && r.modelChecked,"Grid board not SAT for an input");
        for(size_t i=0;i<list.size();++i) require(classicBit(base.rules.symbols[r.labels[r.layout.index(list[i].out)]])==values[inputs+i],"Grid gate output disagrees with NOR reference");
        for(int o=0;o<outputs;++o) {
            int expected=values[values.size()-1-o]^(o%2==1);
            require(classicBit(base.rules.symbols[r.labels[r.layout.index(outCells[o])]])==expected,"Grid output port disagrees");
            auto wrong=base; pinClassicPort(wrong,{"out",outCells[o]},1-expected); ++checks;
            require(solve(wrong).status==SolveStatus::Unsat,"Wrong grid output was SAT");
        }
    }
}
int main(int argc,char** argv){try{
    if(argc>2) {pitchX=std::atoi(argv[1]); pitchY=std::atoi(argv[2]);}
    if(argc>3) window=std::atoi(argv[3]);
    randomCase(1,2,1,1,1);
    randomCase(2,3,6,3,2);
    for(unsigned s=3;s<9;++s) randomCase(s,3,24+static_cast<int>(s)*6,6,3);
    std::cout<<"PASS: "<<checks<<" SAT/UNSAT checks on grid-routed random NOR networks\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
