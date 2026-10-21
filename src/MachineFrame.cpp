#include "MachineFrame.h"
#include "BuildControl.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <map>
#include <set>
#include <stdexcept>
namespace ims {
std::vector<Coord> placeNorGates(const NorNetlist& net,const FrameOptions& o,Coord origin) {
    // Hints are field-cell units; each unit spans `spread` slots. Gates take the
    // nearest free slot to their hint, searched in growing rings.
    float minX=std::numeric_limits<float>::max(),minY=minX;
    for(auto [x,y]:net.hints) {minX=std::min(minX,x); minY=std::min(minY,y);}
    std::set<std::pair<int,int>> used; std::vector<std::pair<int,int>> grid;
    for(size_t g=0;g<net.gates.size();++g) {
        auto [hx,hy]=net.hints[g];
        int tx=static_cast<int>(std::lround((hx-minX)*static_cast<float>(o.spread))),ty=static_cast<int>(std::lround((hy-minY)*static_cast<float>(o.spread)));
        bool placed=false;
        for(int r=0;!placed;++r) for(int dy=-r;dy<=r && !placed;++dy) for(int dx=-r;dx<=r && !placed;++dx) {
            if(std::max(std::abs(dx),std::abs(dy))!=r) continue;
            int sx=tx+dx,sy=ty+dy; if(sx<0 || sy<0) continue;
            if(used.insert({sx,sy}).second) {grid.push_back({sx,sy}); placed=true;}
        }
    }
    // Drop slot columns and rows that received no gate.
    std::set<int> xs,ys; for(auto [x,y]:grid) {xs.insert(x); ys.insert(y);}
    std::map<int,int> cx,cy; for(int x:xs) cx.emplace(x,static_cast<int>(cx.size())); for(int y:ys) cy.emplace(y,static_cast<int>(cy.size()));
    std::vector<Coord> slots;
    for(auto [x,y]:grid) slots.push_back({origin.x+static_cast<int64_t>(cx[x])*o.pitchX,origin.y+static_cast<int64_t>(cy[y])*o.pitchY});
    return slots;
}
std::vector<Coord> placeInOrder(size_t gates,const FrameOptions& o,Coord origin) {
    // Rows of a near-square board in creation order, alternating direction so
    // consecutive gates stay neighbours.
    size_t columns=std::max<size_t>(1,static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(gates)*o.pitchY/o.pitchX))));
    std::vector<Coord> slots;
    for(size_t g=0;g<gates;++g) {
        size_t row=g/columns,k=g%columns,column=row%2?columns-1-k:k;
        slots.push_back({origin.x+static_cast<int64_t>(column)*o.pitchX,origin.y+static_cast<int64_t>(row)*o.pitchY});
    }
    return slots;
}
std::vector<Coord> placeAnnealed(const FrameCircuit& circuit,const FrameOptions& o,Coord origin) {
    // Simulated annealing of gates on a slot grid (with some empty slots),
    // minimising the half-perimeter wire length of every net. State inputs
    // pull towards the left edge, state outputs to the right and display
    // outputs to the bottom, where their ports go. Hinted circuits start from
    // their hints and are only refined locally.
    const auto& net=circuit.net; const size_t n=net.gates.size(),inputs=net.inputs.size();
    if(n<2) return circuit.hinted?placeNorGates(net,o,origin):placeInOrder(n,o,origin);
    std::vector<int> gx(n),gy(n); int cols=0,rows=0;
    if(circuit.hinted) {
        auto hinted=placeNorGates(net,o,origin);
        for(size_t g=0;g<n;++g) {gx[g]=static_cast<int>((hinted[g].x-origin.x)/o.pitchX); gy[g]=static_cast<int>((hinted[g].y-origin.y)/o.pitchY); cols=std::max(cols,gx[g]+1); rows=std::max(rows,gy[g]+1);}
    } else {
        const size_t want=n+n/8+4;
        cols=std::max(2,static_cast<int>(std::ceil(std::sqrt(static_cast<double>(want)*o.pitchY/o.pitchX)))); rows=static_cast<int>((want+static_cast<size_t>(cols)-1)/static_cast<size_t>(cols));
        for(size_t g=0;g<n;++g) {int row=static_cast<int>(g/static_cast<size_t>(cols)),k=static_cast<int>(g%static_cast<size_t>(cols)); gx[g]=row%2?cols-1-k:k; gy[g]=row;}
    }
    const size_t slots=static_cast<size_t>(cols)*static_cast<size_t>(rows);
    struct Net {std::vector<uint32_t> gates;bool left{},right{},bottom{};double cost{};};
    std::vector<Net> nets(inputs+n);
    for(size_t g=0;g<n;++g) {
        nets[inputs+g].gates.push_back(static_cast<uint32_t>(g));
        for(auto s:net.gates[g]) if(!s.constant()) {auto& v=nets[static_cast<size_t>(s.id)].gates; if(v.empty() || v.back()!=g) v.push_back(static_cast<uint32_t>(g));}
    }
    for(size_t i=0;i<circuit.stateBits;++i) nets[i].left=true;
    for(size_t i=0;i<net.outputs.size();++i) if(!net.outputs[i].second.constant()) (i<circuit.stateBits?nets[static_cast<size_t>(net.outputs[i].second.id)].right:nets[static_cast<size_t>(net.outputs[i].second.id)].bottom)=true;
    // Very wide nets span the board anyway; they only slow the moves down.
    std::vector<std::vector<uint32_t>> netsOf(n);
    for(size_t k=0;k<nets.size();++k) if(nets[k].gates.size()+nets[k].left+nets[k].right+nets[k].bottom>=2 && nets[k].gates.size()<=96)
        for(auto g:nets[k].gates) if(netsOf[g].empty() || netsOf[g].back()!=k) netsOf[g].push_back(static_cast<uint32_t>(k));
    std::vector<int64_t> at(slots,-1);
    for(size_t g=0;g<n;++g) at[static_cast<size_t>(gy[g])*cols+gx[g]]=static_cast<int64_t>(g);
    auto cost=[&](const Net& e){
        int x0=e.left?-1:cols,x1=e.right?cols:-1,y0=rows,y1=e.bottom?rows:-1;
        for(auto g:e.gates) {x0=std::min(x0,gx[g]); x1=std::max(x1,gx[g]); y0=std::min(y0,gy[g]); y1=std::max(y1,gy[g]);}
        if(e.gates.empty()) return 0.0;
        return static_cast<double>(x1-x0)*o.pitchX+static_cast<double>(std::max(0,y1-y0))*o.pitchY;
    };
    double total=0; for(auto& e:nets) {e.cost=cost(e); total+=e.cost;}
    std::mt19937 rng(12345); std::vector<uint32_t> touched; std::vector<uint32_t> mark(nets.size(),0); uint32_t stamp=0;
    // Try moving gate g to slot (x,y), swapping with its occupant; returns the cost change and applies it.
    auto move=[&](size_t g,int x,int y,double temperature){
        size_t to=static_cast<size_t>(y)*cols+x; int64_t other=at[to];
        int ox=gx[g],oy=gy[g]; size_t from=static_cast<size_t>(oy)*cols+ox;
        ++stamp; touched.clear();
        auto collect=[&](size_t h){for(auto k:netsOf[h]) if(mark[k]!=stamp) {mark[k]=stamp; touched.push_back(k);}};
        collect(g); if(other>=0) collect(static_cast<size_t>(other));
        gx[g]=x; gy[g]=y; if(other>=0) {gx[static_cast<size_t>(other)]=ox; gy[static_cast<size_t>(other)]=oy;}
        double delta=0; for(auto k:touched) delta+=cost(nets[k])-nets[k].cost;
        if(delta<=0 || std::uniform_real_distribution<double>(0,1)(rng)<std::exp(-delta/temperature)) {
            for(auto k:touched) nets[k].cost=cost(nets[k]);
            at[to]=static_cast<int64_t>(g); at[from]=other; total+=delta; return true;
        }
        gx[g]=ox; gy[g]=oy; if(other>=0) {gx[static_cast<size_t>(other)]=x; gy[static_cast<size_t>(other)]=y;}
        return false;
    };
    const double startCost=total;
    double temperature=static_cast<double>(o.pitchX)*(circuit.hinted?0.25:4); double range=circuit.hinted?3:std::max(cols,rows);
    const size_t perStep=std::max<size_t>(n*20,2000);
    for(int step=0;step<400 && temperature>0.5;++step) {
        if(step%8==0) buildCheckpoint("Placement",static_cast<size_t>(step),400);
        size_t accepted=0; int r=std::max(1,static_cast<int>(range));
        for(size_t m=0;m<perStep;++m) {
            size_t g=rng()%n;
            int x=std::clamp(gx[g]+static_cast<int>(rng()%(2*r+1))-r,0,cols-1),y=std::clamp(gy[g]+static_cast<int>(rng()%(2*r+1))-r,0,rows-1);
            if(x==gx[g] && y==gy[g]) continue;
            accepted+=move(g,x,y,temperature);
        }
        // Keep about 44% of moves accepted (VPR's schedule).
        double rate=static_cast<double>(accepted)/static_cast<double>(perStep);
        range=std::clamp(range*(1-0.44+rate),1.0,static_cast<double>(std::max(cols,rows)));
        temperature*=rate>0.96?0.5:rate>0.8?0.9:rate>0.15?0.95:0.8;
    }
    // Routing converges when wires cover at most about 13% of the slot area
    // (measured on the MineC examples); denser placements get wider spacing.
    // Spreading by f multiplies the wire length by f and the area by f^2.
    const double density=total/(static_cast<double>(slots)*o.pitchX*o.pitchY),f=std::max(1.0,density/0.13);
    const int64_t px=static_cast<int64_t>(std::ceil(o.pitchX*f/4))*4,py=static_cast<int64_t>(std::ceil(o.pitchY*f/4))*4;
    if(size_t size=0; getenv_s(&size,nullptr,0,"IMS_GRID_DUMP")==0 && size>0) std::fprintf(stderr,"anneal: %zu gates on %dx%d slots, wire %.0f -> %.0f cells, density %.3f, pitch %lldx%lld\n",n,cols,rows,startCost,total,density,px,py);
    std::vector<Coord> out; out.reserve(n);
    for(size_t g=0;g<n;++g) out.push_back({origin.x+gx[g]*px,origin.y+gy[g]*py});
    return out;
}
namespace {
// Positions on a port line, spacing 8, kept in the order of the desired values
// and as close to them as the spacing allows.
std::vector<int64_t> spreadPorts(const std::vector<double>& desired,int64_t first) {
    std::vector<size_t> order(desired.size()); std::iota(order.begin(),order.end(),0);
    std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return desired[a]<desired[b];});
    std::vector<int64_t> at(desired.size()); int64_t next=first;
    for(size_t i:order) {int64_t want=static_cast<int64_t>(std::llround(desired[i]/8.0))*8; int64_t p=std::max(next,want); at[i]=p; next=p+8;}
    return at;
}
}
MachineFrame layoutFrame(const FrameCircuit& circuit,FrameOptions o) {
    if(o.pitchX%4 || o.pitchY%4 || o.pitchX<24 || o.pitchY<12) throw std::invalid_argument("Frame pitch must be divisible by 4 and at least 24x12");
    MachineFrame f; f.circuit=circuit; const auto& net=f.circuit.net;
    const size_t state=circuit.stateBits,keys=circuit.keys.size(),inputs=net.inputs.size();
    if(state+keys!=inputs || net.outputs.size()!=state+static_cast<size_t>(circuit.displayWidth*circuit.displayHeight)) throw std::invalid_argument("Frame circuit ports do not match its netlist");
    if(net.gates.empty()) throw std::invalid_argument("A frame needs at least one gate");
    buildCheckpoint("Placement");
    const Coord origin{64,66};
    auto slots=placeAnnealed(circuit,o,origin);
    int64_t maxX=origin.x,maxY=origin.y; for(auto s:slots) {maxX=std::max(maxX,s.x); maxY=std::max(maxY,s.y);}
    // Where each input is read and each output is driven, from the placement.
    std::vector<double> readX(inputs,0),readY(inputs,0),readN(inputs,0);
    for(size_t g=0;g<net.gates.size();++g) for(auto s:net.gates[g]) if(!s.constant() && static_cast<size_t>(s.id)<inputs) {
        auto i=static_cast<size_t>(s.id); readX[i]+=static_cast<double>(slots[g].x); readY[i]+=static_cast<double>(slots[g].y)+3; ++readN[i];
    }
    auto readAt=[&](size_t i,bool y){return readN[i]>0?(y?readY[i]:readX[i])/readN[i]:static_cast<double>(y?origin.y:origin.x);};
    auto driverAt=[&](NorSignal s)->Coord {
        if(s.constant()) return {origin.x,origin.y};
        if(static_cast<size_t>(s.id)<inputs) return {static_cast<int64_t>(readAt(static_cast<size_t>(s.id),false)),static_cast<int64_t>(readAt(static_cast<size_t>(s.id),true))};
        return slots[static_cast<size_t>(s.id)-inputs];
    };
    f.left=0; f.right=(maxX+48)/4*4;
    std::vector<double> rowWant(state);
    for(size_t i=0;i<state;++i) rowWant[i]=(readAt(i,true)+static_cast<double>(driverAt(net.outputs[i].second).y)+4)/2;
    auto rows=spreadPorts(rowWant,16);
    // Keys on the top edge, plus a constant-zero source when an output is constant.
    bool needZero=false; for(const auto& out:net.outputs) needZero|=out.second.constant();
    std::vector<double> keyWant(keys+(needZero?1:0)); for(size_t k=0;k<keys;++k) keyWant[k]=readAt(state+k,false);
    if(needZero) keyWant.back()=static_cast<double>(origin.x);
    auto keyX=spreadPorts(keyWant,origin.x);
    const size_t cells=net.outputs.size()-state;
    std::vector<double> showWant(cells); for(size_t i=0;i<cells;++i) showWant[i]=static_cast<double>(driverAt(net.outputs[state+i].second).x)+6;
    auto showX=spreadPorts(showWant,origin.x);
    const int64_t top=8,bottom=(std::max(maxY,rows.empty()?0:*std::max_element(rows.begin(),rows.end()))+40)/4*4;
    int64_t widest=origin.x; for(auto x:showX) widest=std::max(widest,x); for(auto x:keyX) widest=std::max(widest,x);
    if(widest>=f.right-16) f.right=(widest+32)/4*4;
    GridLayout g; std::vector<GridLayout::Signal> signal(inputs+net.gates.size());
    for(size_t i=0;i<state;++i) {Coord c{f.left,rows[i]}; f.stateIn.push_back(c); signal[i]={g.input(c,GridLayout::Side::Right),false};}
    for(size_t k=0;k<keys;++k) {Coord c{keyX[k],top}; f.keys.push_back(c); signal[state+k]={g.input(c,GridLayout::Side::Down),false};}
    GridLayout::Signal zero{}; Coord zeroCell{};
    if(needZero) {zeroCell={keyX.back(),top}; zero={g.input(zeroCell,GridLayout::Side::Down),false};}
    auto resolve=[&](NorSignal s)->GridLayout::Signal {
        if(s.constant()) return {zero.net,s.inverted};
        auto base=signal[static_cast<size_t>(s.id)]; base.inverted^=s.inverted; return base;
    };
    for(size_t gi=0;gi<net.gates.size();++gi) signal[inputs+gi]={g.nor(resolve(net.gates[gi][0]),resolve(net.gates[gi][1]),slots[gi]),false};
    for(size_t i=0;i<state;++i) {Coord c{f.right,rows[i]}; f.stateOut.push_back(c); g.output(resolve(net.outputs[i].second),c,GridLayout::Side::Right);}
    for(size_t i=0;i<cells;++i) {Coord c{showX[i],bottom}; f.display.push_back(c); g.output(resolve(net.outputs[state+i].second),c,GridLayout::Side::Down);}
    g.frameColumns(f.left,f.right);
    f.grid=g.compileGrid(&f.info);
    if(needZero) {
        // The zero source is a revealed safe cell: its digit counts the fixed mines around it.
        int mines=0;
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) {
            int8_t c=f.grid.at({zeroCell.x+dx,zeroCell.y+dy});
            if(c<0) throw std::logic_error("Constant source has an unknown neighbour");
            mines+=c==9;
        }
        f.grid.cells[static_cast<size_t>((zeroCell.y-f.grid.origin.y)*f.grid.width+(zeroCell.x-f.grid.origin.x))]=static_cast<int8_t>(mines);
    }
    f.stride=f.right-f.left;
    return f;
}
namespace {
constexpr uint64_t frameFormat=4;
void mix(uint64_t& h,uint64_t v) {h^=v+0x9E3779B97F4A7C15ull+(h<<6)+(h>>2);}
template<class T> void put(std::ostream& o,T v) {o.write(reinterpret_cast<const char*>(&v),sizeof v);}
template<class T> T get(std::istream& i) {T v{}; i.read(reinterpret_cast<char*>(&v),sizeof v); return v;}
void putCoords(std::ostream& o,const std::vector<Coord>& v) {put<uint64_t>(o,v.size()); for(auto c:v) {put(o,c.x); put(o,c.y);}}
std::vector<Coord> getCoords(std::istream& i) {auto n=get<uint64_t>(i); if(n>1u<<20) throw std::runtime_error("Bad frame cache"); std::vector<Coord> v(n); for(auto& c:v) {c.x=get<int64_t>(i); c.y=get<int64_t>(i);} return v;}
}
uint64_t frameKey(const FrameCircuit& c,const FrameOptions& o) {
    uint64_t h=frameFormat;
    for(int v:{o.pitchX,o.pitchY,o.spread,c.displayWidth,c.displayHeight}) mix(h,static_cast<uint64_t>(v));
    mix(h,c.stateBits); mix(h,c.keys.size()); mix(h,c.hinted);
    mix(h,0xA11EA1ull);   // placement refined by annealing, spacing from wire density
    for(const auto& g:c.net.gates) for(auto s:g) mix(h,static_cast<uint64_t>(s.id)*2+s.inverted);
    if(c.hinted) for(auto [x,y]:c.net.hints) {mix(h,static_cast<uint64_t>(std::llround(x*16))); mix(h,static_cast<uint64_t>(std::llround(y*16)));}
    for(const auto& [n,s]:c.net.outputs) mix(h,static_cast<uint64_t>(s.id)*2+s.inverted);
    return h;
}
void saveFrame(const std::filesystem::path& path,const MachineFrame& f,const FrameOptions& o) {
    std::filesystem::create_directories(path.parent_path());
    auto temp=path; temp+=".part";
    {
        std::ofstream out(temp,std::ios::binary); if(!out) throw std::runtime_error("Cannot write frame cache");
        out.write("IMSFRAME",8); put(out,frameKey(f.circuit,o));
        put(out,f.grid.origin.x); put(out,f.grid.origin.y); put(out,f.grid.width); put(out,f.grid.height);
        put(out,f.left); put(out,f.right); put(out,f.stride);
        put<uint64_t>(out,f.info.gates); put<uint64_t>(out,f.info.crossings); put<uint64_t>(out,f.info.adapters); put<uint64_t>(out,f.info.retries); put(out,f.info.routeMs);
        putCoords(out,f.stateIn); putCoords(out,f.stateOut); putCoords(out,f.keys); putCoords(out,f.display);
        out.write(reinterpret_cast<const char*>(f.grid.cells.data()),static_cast<std::streamsize>(f.grid.cells.size()));
        if(!out) throw std::runtime_error("Cannot write frame cache");
    }
    std::filesystem::rename(temp,path);
}
std::optional<MachineFrame> loadFrame(const std::filesystem::path& path,const FrameCircuit& circuit,const FrameOptions& o) {
    std::ifstream in(path,std::ios::binary); if(!in) return std::nullopt;
    char magic[8]{}; in.read(magic,8); if(std::string(magic,8)!="IMSFRAME") return std::nullopt;
    MachineFrame f; f.circuit=circuit;
    if(get<uint64_t>(in)!=frameKey(circuit,o)) return std::nullopt;
    f.grid.origin.x=get<int64_t>(in); f.grid.origin.y=get<int64_t>(in); f.grid.width=get<int>(in); f.grid.height=get<int>(in);
    f.left=get<int64_t>(in); f.right=get<int64_t>(in); f.stride=get<int64_t>(in);
    f.info.gates=get<uint64_t>(in); f.info.crossings=get<uint64_t>(in); f.info.adapters=get<uint64_t>(in); f.info.retries=get<uint64_t>(in); f.info.routeMs=get<double>(in);
    f.info.width=f.grid.width; f.info.height=f.grid.height; f.info.nets=circuit.net.inputs.size()+circuit.net.gates.size();
    f.stateIn=getCoords(in); f.stateOut=getCoords(in); f.keys=getCoords(in); f.display=getCoords(in);
    if(f.grid.width<1 || f.grid.height<1 || static_cast<int64_t>(f.grid.width)*f.grid.height>256ll*1024*1024) return std::nullopt;
    f.grid.cells.resize(static_cast<size_t>(f.grid.width)*f.grid.height);
    in.read(reinterpret_cast<char*>(f.grid.cells.data()),static_cast<std::streamsize>(f.grid.cells.size()));
    if(!in || f.stateIn.size()!=circuit.stateBits || f.keys.size()!=circuit.keys.size()) return std::nullopt;
    return f;
}
MachineFrame obtainFrame(const FrameCircuit& circuit,const FrameOptions& o,const std::filesystem::path& cacheDir) {
    std::string stem; for(char c:circuit.name) stem+=std::isalnum(static_cast<unsigned char>(c))?static_cast<char>(std::tolower(static_cast<unsigned char>(c))):'-';
    char key[20]; std::snprintf(key,sizeof(key),"%016llx",static_cast<unsigned long long>(frameKey(circuit,o)));
    auto path=cacheDir/(stem+"-"+key+".frame");
    if(auto f=loadFrame(path,circuit,o)) return std::move(*f);
    auto f=layoutFrame(circuit,o);
    try {saveFrame(path,f,o);} catch(const std::exception&) {}
    return f;
}
}
