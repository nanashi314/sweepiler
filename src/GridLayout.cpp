#include "GridLayout.h"
#include "ClassicKernels.h"
#include "BuildControl.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <limits>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
namespace ims {
namespace {
// Routes run on a coarse grid of pitch 4 cells. A coarse edge is two ordinary
// inverting links through its middle lattice cell. Nets turn and branch only at
// coarse nodes, and two nets share a node only as an orthogonal straight
// crossing (Thieme/Basten Fig.2), whose arms are the middle cells. With pitch 4
// no crossing corner can touch another route's clue, as in ClassicLayout.
constexpr int dx4[4]={1,-1,0,0},dy4[4]={0,0,1,-1};
struct C {int x{},y{}; bool operator==(const C&) const=default;}; // cell coordinates (local)
bool gridDumpRequested() {size_t size=0; return getenv_s(&size,nullptr,0,"IMS_GRID_DUMP")==0 && size>0;}
C unit(GridLayout::Side s) {
    switch(s) {case GridLayout::Side::Left:return {-1,0};case GridLayout::Side::Right:return {1,0};case GridLayout::Side::Up:return {0,-1};default:return {0,1};}
}
struct Link {C a,b;};
// Sink: route reaches pin; fixed links lead to port. links[adapter] may become
// two links through zone (middle, clue next to a, clue next to b).
struct Stub {int net{};bool inverted{};C pin,port;std::vector<Link> links;size_t adapter{};C middle,first,second;};

class Coarse {
public:
    Coarse(int w,int h):w_(w),h_(h),n_(static_cast<size_t>(w)*h),hOwner_(n_,-1),vOwner_(n_,-1),hBlock_(n_,0),vBlock_(n_,0),
        nodeBlock_(n_,0),only_(n_,-1),netA_(n_,-1),netB_(n_,-1) {}
    int w() const {return w_;}
    int h() const {return h_;}
    bool in(int x,int y) const {return x>=1 && y>=1 && x<w_-1 && y<h_-1;}
    size_t id(int x,int y) const {return static_cast<size_t>(y)*w_+x;}
    static bool horizontal(int k) {return k<2;}
    size_t edge(int x,int y,int k) const {return k==0?id(x,y):k==1?id(x-1,y):k==2?id(x,y):id(x,y-1);}
    int owner(int x,int y,int k) const {return horizontal(k)?hOwner_[edge(x,y,k)]:vOwner_[edge(x,y,k)];}
    bool edgeBlocked(int x,int y,int k) const {return horizontal(k)?hBlock_[edge(x,y,k)]:vBlock_[edge(x,y,k)];}
    void blockEdge(int x,int y,int k) {if(in(x,y) && in(x+dx4[k],y+dy4[k])) (horizontal(k)?hBlock_:vBlock_)[edge(x,y,k)]=1;}
    void blockNode(int x,int y) {if(!in(x,y)) return; nodeBlock_[id(x,y)]=1; for(int k=0;k<4;++k) blockEdge(x,y,k);}
    bool nodeBlocked(int x,int y) const {return nodeBlock_[id(x,y)]!=0;}
    // Pins and ports belong to one net; others may neither enter nor cross them.
    void keep(int x,int y,int net) {if(!in(x,y)) return; if(only_[id(x,y)]>=0 && only_[id(x,y)]!=net) throw std::invalid_argument("Grid pins overlap"); only_[id(x,y)]=net;}
    int kept(int x,int y) const {return only_[id(x,y)];}
    int netA(int x,int y) const {return netA_[id(x,y)];}
    int netB(int x,int y) const {return netB_[id(x,y)];}
    bool crossing(int x,int y) const {return netB_[id(x,y)]>=0;}
    bool edgeFree(int x,int y,int k) const {return in(x,y) && in(x+dx4[k],y+dy4[k]) && !edgeBlocked(x,y,k) && owner(x,y,k)<0;}
    // A node a net may enter and own: unblocked, unused and not kept for another net.
    bool nodeFree(int x,int y,int net) const {return in(x,y) && !nodeBlocked(x,y) && netA(x,y)<0 && (kept(x,y)<0 || kept(x,y)==net);}
    int degree(int x,int y,int net) const {int d=0; for(int k=0;k<4;++k) if(in(x+dx4[k],y+dy4[k]) && owner(x,y,k)==net) ++d; return d;}
    int straightAxis(int x,int y,int net) const {
        if(degree(x,y,net)!=2) return -1;
        if(owner(x,y,0)==net && owner(x,y,1)==net) return 0;
        if(owner(x,y,2)==net && owner(x,y,3)==net) return 1;
        return -1;
    }
    // Crossable for net moving along axis: one other straight orthogonal net.
    bool crossable(int x,int y,int net,int axis) const {
        if(!in(x,y) || nodeBlocked(x,y) || kept(x,y)>=0 || crossing(x,y)) return false;
        int other=netA(x,y); return other>=0 && other!=net && straightAxis(x,y,other)==1-axis;
    }
    void setEdge(int x,int y,int k,int net) {
        size_t e=edge(x,y,k); auto& o=horizontal(k)?hOwner_[e]:vOwner_[e]; log({horizontal(k)?0:1,e,o}); o=net;
        attach(x,y,net); attach(x+dx4[k],y+dy4[k],net);
    }
    void clearEdge(int x,int y,int k) {
        size_t e=edge(x,y,k); auto& o=horizontal(k)?hOwner_[e]:vOwner_[e]; int net=o; log({horizontal(k)?0:1,e,o}); o=-1;
        if(degree(x,y,net)==0) detach(x,y,net);
        if(degree(x+dx4[k],y+dy4[k],net)==0) detach(x+dx4[k],y+dy4[k],net);
    }
    void begin() {journal_.clear(); journaling_=true;}
    void commit() {journaling_=false; journal_.clear();}
    void rollback() {
        for(auto it=journal_.rbegin();it!=journal_.rend();++it) (it->kind==0?hOwner_:it->kind==1?vOwner_:it->kind==2?netA_:netB_)[it->index]=it->old;
        journaling_=false; journal_.clear();
    }
private:
    struct Entry {int kind;size_t index;int old;};
    void log(Entry e) {if(journaling_) journal_.push_back(e);}
    void attach(int x,int y,int net) {
        size_t i=id(x,y); if(netA_[i]==net || netB_[i]==net) return;
        if(netA_[i]<0) {log({2,i,netA_[i]}); netA_[i]=net;} else {log({3,i,netB_[i]}); netB_[i]=net;}
    }
    void detach(int x,int y,int net) {
        size_t i=id(x,y);
        if(netB_[i]==net) {log({3,i,netB_[i]}); netB_[i]=-1;}
        else if(netA_[i]==net) {log({2,i,netA_[i]}); log({3,i,netB_[i]}); netA_[i]=netB_[i]; netB_[i]=-1;}
    }
    int w_,h_; size_t n_;
    std::vector<int> hOwner_,vOwner_;
    std::vector<uint8_t> hBlock_,vBlock_,nodeBlock_;
    std::vector<int> only_,netA_,netB_;
    bool journaling_{};std::vector<Entry> journal_;
};
}

int GridLayout::input(Coord port,Side exit) {
    if((port.x|port.y)&3) throw std::invalid_argument("Grid ports use coordinates divisible by 4");
    drivers_.push_back({port,exit,-1}); return static_cast<int>(drivers_.size()-1);
}
int GridLayout::nor(Signal a,Signal b,Coord slot) {
    if((slot.x&3) || ((slot.y-2)&3)) throw std::invalid_argument("Grid slots need x = 0 and y = 2 modulo 4");
    for(auto s:{a,b}) if(s.net<0 || static_cast<size_t>(s.net)>=drivers_.size()) throw std::invalid_argument("Unknown grid net");
    int net=static_cast<int>(drivers_.size());
    gates_.push_back({a,b,slot,net});
    drivers_.push_back({{slot.x+6,slot.y+4},Side::Right,static_cast<int>(gates_.size()-1)});
    return net;
}
void GridLayout::output(Signal s,Coord port,Side outward) {
    if((port.x|port.y)&3) throw std::invalid_argument("Grid ports use coordinates divisible by 4");
    if(s.net<0 || static_cast<size_t>(s.net)>=drivers_.size()) throw std::invalid_argument("Unknown grid net");
    outputs_.push_back({s,port,outward});
}

CellGrid GridLayout::compileGrid(GridInfo* info) const {
    const auto start=std::chrono::steady_clock::now();
    buildCheckpoint("Placement");
    if(drivers_.empty()) throw std::invalid_argument("Empty grid layout");
    int64_t minX=std::numeric_limits<int64_t>::max(),minY=minX,maxX=std::numeric_limits<int64_t>::min(),maxY=maxX;
    auto extend=[&](Coord c){minX=std::min(minX,c.x);minY=std::min(minY,c.y);maxX=std::max(maxX,c.x);maxY=std::max(maxY,c.y);};
    for(const auto& g:gates_) {extend({g.slot.x-12,g.slot.y-2});extend({g.slot.x+12,g.slot.y+10});}
    for(const auto& d:drivers_) if(d.gate<0) extend(d.at);
    for(const auto& o:outputs_) extend(o.port);
    const int64_t margin=16;
    auto down4=[](int64_t v){return v>=0?v/4*4:-((-v+3)/4*4);};
    Coord origin{down4(minX-margin),down4(minY-margin)};
    int64_t cw=(maxX+margin-origin.x)/4+2,ch=(maxY+margin-origin.y)/4+2;
    if(cw*4>32768 || ch*4>32768 || cw*ch*16>64ll*1024*1024) throw std::invalid_argument("Grid layout exceeds the finite area budget");
    Coarse G(static_cast<int>(cw),static_cast<int>(ch));
    auto local=[&](Coord c){return C{static_cast<int>(c.x-origin.x),static_cast<int>(c.y-origin.y)};};
    auto global=[&](C c){return Coord{c.x+origin.x,c.y+origin.y};};
    auto node=[](C c){return C{c.x/4,c.y/4};}; // cell -> coarse node (cell divisible by 4)
    const int nets=static_cast<int>(drivers_.size());
    std::vector<C> driverCell(nets),driverNode(nets);
    std::vector<std::vector<Link>> fixedLinks(nets); // driver stubs (no adapters)
    std::vector<Stub> stubs;
    for(size_t g=0;g<gates_.size();++g) {
        if(g%256==0) buildCheckpoint("Kernels",g,gates_.size());
        const auto& gate=gates_[g]; C o=local(gate.slot); auto p=[&](int x,int y){return C{o.x+x,o.y+y};};
        auto n=[&](int x,int y){return node(p(x,y));};
        // Kernel body nodes, the B-stub corner edge, the Y-stub edge, and edges
        // whose middle cells would touch the adapter zones.
        for(auto c:{n(-4,2),n(0,2),n(4,2),n(-4,6)}) {if(G.nodeBlocked(c.x,c.y) || G.netA(c.x,c.y)>=0) throw std::invalid_argument("Overlapping grid gates"); G.blockNode(c.x,c.y);}
        C a=n(-8,2),b=n(-8,6),y=n(8,2);
        G.blockEdge(a.x,a.y,2); G.blockEdge(y.x,y.y,2);
        G.keep(a.x,a.y,gate.a.net); G.keep(b.x,b.y,gate.b.net); G.keep(y.x,y.y,gate.net);
        stubs.push_back({gate.a.net,gate.a.inverted,p(-8,2),p(-2,2),{{p(-8,2),p(-6,2)},{p(-6,2),p(-4,2)},{p(-4,2),p(-2,2)}},1,p(-5,1),p(-6,1),p(-4,1)});
        stubs.push_back({gate.b.net,gate.b.inverted,p(-8,6),p(-2,4),{{p(-8,6),p(-8,4)},{p(-8,4),p(-6,4)},{p(-6,4),p(-4,4)},{p(-4,4),p(-2,4)}},2,p(-5,5),p(-6,5),p(-4,5)});
        driverCell[gate.net]=p(6,4); driverNode[gate.net]=y;
        fixedLinks[gate.net]={{p(6,4),p(8,4)},{p(8,4),p(8,2)}};
    }
    for(int n=0;n<nets;++n) if(drivers_[n].gate<0) {
        driverCell[n]=local(drivers_[n].at); driverNode[n]=node(driverCell[n]);
        if(G.nodeBlocked(driverNode[n].x,driverNode[n].y)) throw std::invalid_argument("Grid input port inside a gate");
        G.keep(driverNode[n].x,driverNode[n].y,n);
        // Inputs leave only towards exit.
        for(int k=0;k<4;++k) {C u=unit(drivers_[n].exit); if(dx4[k]!=u.x || dy4[k]!=u.y) G.blockEdge(driverNode[n].x,driverNode[n].y,k);}
    }
    for(const auto& out:outputs_) {
        C u=unit(out.outward),port=local(out.port),side{std::abs(u.y),std::abs(u.x)};
        C pin{port.x-4*u.x,port.y-4*u.y},pn=node(port),en=node(pin);
        G.keep(pn.x,pn.y,out.s.net); G.blockNode(pn.x,pn.y);
        G.keep(en.x,en.y,out.s.net);
        for(int k=0;k<4;++k) if(dx4[k]==side.x && dy4[k]==side.y) G.blockEdge(en.x,en.y,k);
        C mid{pin.x+2*u.x,pin.y+2*u.y};
        stubs.push_back({out.s.net,out.s.inverted,pin,port,{{pin,mid},{mid,port}},0,{pin.x+u.x+side.x,pin.y+u.y+side.y},{pin.x+side.x,pin.y+side.y},{mid.x+side.x,mid.y+side.y}});
    }
    // Framed layouts keep the port columns and everything outside them route-free.
    if(framed_) {
        if((frameLeft_|frameRight_)&3) throw std::invalid_argument("Frame columns must be divisible by 4");
        int left=static_cast<int>((frameLeft_-origin.x)/4),right=static_cast<int>((frameRight_-origin.x)/4);
        for(int y=0;y<G.h();++y) for(int x=0;x<G.w();++x) if((x<=left || x>=right) && G.in(x,y) && G.kept(x,y)<0) G.blockNode(x,y);
        for(int n=0;n<nets;++n) if(drivers_[n].gate<0 && (driverNode[n].x<left || driverNode[n].x>right)) throw std::invalid_argument("Input port outside the frame");
    }
    // Negotiated congestion (PathFinder): every round reroutes nets with
    // overlaps allowed at a price that rises with present and past overuse.
    // A coarse node has two resources: H (straight horizontal pass) and V
    // (straight vertical pass); a turn, branch or end uses both. A node shared by
    // one H user and one V user is a legal crossing.
    std::vector<std::vector<size_t>> sinksOf(nets);
    for(size_t i=0;i<stubs.size();++i) sinksOf[stubs[i].net].push_back(i);
    const int W=G.w(),H=G.h(); const bool debug=gridDumpRequested();
    const size_t cells=static_cast<size_t>(W)*H;
    std::vector<int> useH(cells,0),useV(cells,0),useE[2]={std::vector<int>(cells,0),std::vector<int>(cells,0)};
    std::vector<float> histH(cells,0),histV(cells,0),histE[2]={std::vector<float>(cells,0),std::vector<float>(cells,0)};
    struct Route {std::vector<std::pair<C,int>> edges;std::vector<std::pair<size_t,uint8_t>> nodes;};
    std::vector<Route> routes(nets);
    auto edgeSlot=[&](int x,int y,int k){return std::pair<int,size_t>{Coarse::horizontal(k)?0:1,G.edge(x,y,k)};};
    // Node usage of a routed net: one axis if it passes straight, else both.
    std::vector<uint8_t> degree(cells,0); std::vector<size_t> touched;
    auto usage=[&](int net){
        touched.clear();
        auto add=[&](size_t i,unsigned bit){if(!degree[i]) touched.push_back(i); degree[i]|=static_cast<uint8_t>(bit);};
        for(auto [c,k]:routes[net].edges) {add(G.id(c.x,c.y),1u<<k); add(G.id(c.x+dx4[k],c.y+dy4[k]),1u<<(k^1));}
        std::vector<std::pair<size_t,uint8_t>> out; out.reserve(touched.size());
        for(size_t i:touched) {uint8_t d=degree[i]; out.push_back({i,static_cast<uint8_t>(d==3?1:d==12?2:3)}); degree[i]=0;}
        return out;
    };
    auto apply=[&](int net,int sign){
        for(auto [c,k]:routes[net].edges) {auto [o,e]=edgeSlot(c.x,c.y,k); useE[o][e]+=sign;}
        for(auto [i,m]:routes[net].nodes) {if(m&1) useH[i]+=sign; if(m&2) useV[i]+=sign;}
    };
    const size_t states=cells*5;
    std::vector<double> dist(states);std::vector<int64_t> from(states);std::vector<uint32_t> seen(states,0);uint32_t epoch=0;
    float present=0.5f;
    std::vector<uint32_t> treeMark(cells,0); uint32_t treeStamp=0; std::vector<size_t> tree;
    auto routeNet=[&](int net){
        routes[net]={};
        ++treeStamp; tree.clear();
        auto inTree=[&](size_t i){return treeMark[i]==treeStamp;};
        auto addTree=[&](size_t i){if(!inTree(i)) {treeMark[i]=treeStamp; tree.push_back(i);}};
        addTree(G.id(driverNode[net].x,driverNode[net].y));
        std::vector<size_t> order=sinksOf[net]; C d=driverNode[net];
        std::sort(order.begin(),order.end(),[&](size_t a,size_t b){auto dd=[&](size_t i){C e=node(stubs[i].pin);return std::abs(e.x-d.x)+std::abs(e.y-d.y);};return dd(a)<dd(b);});
        for(size_t si:order) {
            const C target=node(stubs[si].pin); const size_t tid=G.id(target.x,target.y);
            if(inTree(tid)) continue;
            struct Item {double f,d;size_t st; bool operator>(const Item& o) const {return f>o.f;}};
            auto h=[&](size_t i){int x=static_cast<int>(i%W),y=static_cast<int>(i/W);return 4.0*static_cast<double>(std::abs(x-target.x)+std::abs(y-target.y));};
            // Search a box around the tree and target first; the whole grid only if that fails.
            int bx0=target.x,bx1=target.x,by0=target.y,by1=target.y;
            for(size_t s:tree) {int x=static_cast<int>(s%W),y=static_cast<int>(s/W); bx0=std::min(bx0,x); bx1=std::max(bx1,x); by0=std::min(by0,y); by1=std::max(by1,y);}
            int64_t goal=-1;
            for(int pass=0;pass<2 && goal<0;++pass) {
            const int margin=pass?1<<20:24;
            const int lx=bx0-margin,hx=bx1+margin,ly=by0-margin,hy=by1+margin;
            ++epoch; std::priority_queue<Item,std::vector<Item>,std::greater<>> open;
            for(size_t s:tree) {size_t st=s*5+4; seen[st]=epoch; dist[st]=0; from[st]=-1; open.push({h(s),0.0,st});}
            while(!open.empty()) {
                auto [f,d0,st]=open.top(); open.pop(); (void)f;
                size_t i=st/5; int din=static_cast<int>(st%5);
                if(d0>dist[st]) continue;
                if(i==tid) {goal=static_cast<int64_t>(st); break;}
                int x=static_cast<int>(i%W),y=static_cast<int>(i/W);
                for(int k=0;k<4;++k) {
                    if(din<4 && k==(din^1)) continue;
                    int nx=x+dx4[k],ny=y+dy4[k]; if(!G.in(nx,ny) || nx<lx || nx>hx || ny<ly || ny>hy || G.edgeBlocked(x,y,k)) continue;
                    size_t ni=G.id(nx,ny);
                    if(ni!=tid && (G.nodeBlocked(nx,ny) || (G.kept(nx,ny)>=0 && G.kept(nx,ny)!=net) || inTree(ni))) continue;
                    auto [o,e]=edgeSlot(x,y,k);
                    float c=(4.0f+histE[o][e])*(1.0f+present*static_cast<float>(useE[o][e]));
                    // Price of using this node: straight keeps one axis, turning or branching takes both.
                    bool both=din==4 || ((din<2)!=(k<2));
                    float nodeH=(histH[i]+(useH[i]?4.0f:0.0f))*(1.0f+present*static_cast<float>(useH[i])),nodeV=(histV[i]+(useV[i]?4.0f:0.0f))*(1.0f+present*static_cast<float>(useV[i]));
                    c+=both?nodeH+nodeV:(k<2?nodeH:nodeV);
                    size_t nst=ni*5+static_cast<size_t>(k); double nd=dist[st]+c;
                    if(seen[nst]==epoch && dist[nst]<=nd) continue;
                    seen[nst]=epoch; dist[nst]=nd; from[nst]=static_cast<int64_t>(st); open.push({nd+h(ni),nd,nst});
                }
            }
            }
            if(goal<0) {
                if(debug) std::fprintf(stderr,"unreachable: net %d target %d,%d\n",net,target.x,target.y);
                throw std::invalid_argument("Unroutable grid net "+std::to_string(net));
            }
            for(int64_t st=goal;from[static_cast<size_t>(st)]>=0;st=from[static_cast<size_t>(st)]) {
                size_t i=static_cast<size_t>(st)/5; int k=static_cast<int>(static_cast<size_t>(st)%5);
                C to{static_cast<int>(i%W),static_cast<int>(i/W)}; C fromNode{to.x-dx4[k],to.y-dy4[k]};
                routes[net].edges.push_back({fromNode,k}); addTree(i);
            }
        }
        routes[net].nodes=usage(net);
    };
    std::vector<int> order(nets); for(int n=0;n<nets;++n) order[n]=n;
    std::vector<uint8_t> dirty(nets,1);
    int round=0; size_t best=std::numeric_limits<size_t>::max(); int stall=0;
    for(;;++round) {
        if(round>400) throw std::invalid_argument("Grid routing does not converge");
        size_t rerouted=0;
        for(int net:order) if(dirty[net] && !sinksOf[net].empty()) {
            if(++rerouted%64==0) buildCheckpoint("Routing",rerouted,static_cast<size_t>(nets));
            apply(net,-1); routeNet(net); apply(net,1);
        }
        // Overuse: shared edges, or a node axis used twice.
        size_t over=0; std::fill(dirty.begin(),dirty.end(),0);
        auto mark=[&](std::vector<int>& use,std::vector<float>& hist,size_t i){if(use[i]>1) {hist[i]+=2.0f*static_cast<float>(use[i]-1); ++over; return true;} return false;};
        std::vector<uint8_t> hot(cells,0);
        for(size_t i=0;i<cells;++i) {
            bool a=mark(useH,histH,i),b=mark(useV,histV,i),c=mark(useE[0],histE[0],i),e=mark(useE[1],histE[1],i);
            if(a || b || c || e) hot[i]=1;
        }
        if(debug) std::fprintf(stderr,"round %d: rerouted %zu, overused %zu, %.0f ms\n",round,rerouted,over,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
        if(!over) break;
        // Reroute nets near overuse, in a wider area while progress stalls;
        // every eighth round reroute all while overuse is still widespread.
        if(over<best) {best=over; stall=0;} else ++stall;
        std::vector<uint8_t> near(cells,0);
        const int radius=std::min(8,2+stall/6);
        for(size_t i=0;i<cells;++i) if(hot[i]) {int x=static_cast<int>(i%W),y=static_cast<int>(i/W); for(int oy=-radius;oy<=radius;++oy) for(int ox=-radius;ox<=radius;++ox) if(G.in(x+ox,y+oy)) near[G.id(x+ox,y+oy)]=1;}
        for(int net=0;net<nets;++net) {
            if(round%8==7 && over*4>static_cast<size_t>(nets)) {dirty[net]=1; continue;}
            for(auto [i,m]:routes[net].nodes) if(near[i]) {dirty[net]=1; break;}
        }
        present=std::min(present*1.25f,1e6f);
    }
    // Commit the legal routes to the coarse grid.
    for(int net=0;net<nets;++net) for(auto [c,k]:routes[net].edges) G.setEdge(c.x,c.y,k,net);
    const size_t rips=static_cast<size_t>(round);
    // Expand to ordinary links; crossings replace the two links nearest the node.
    buildCheckpoint("Phase");
    struct Arc {C to;uint8_t inv;};
    std::unordered_map<uint64_t,std::vector<Arc>> graph;
    auto key=[](C c){return (static_cast<uint64_t>(static_cast<uint32_t>(c.x))<<32)|static_cast<uint32_t>(c.y);};
    auto arc=[&](C a,C b,uint8_t inv){graph[key(a)].push_back({b,inv}); graph[key(b)].push_back({a,inv});};
    std::vector<Link> links; std::vector<C> crossings;
    for(int y=1;y<H-1;++y) for(int x=1;x<W-1;++x) {
        if(G.crossing(x,y)) crossings.push_back({x*4,y*4});
        for(int k:{0,2}) {
            if(!G.in(x+dx4[k],y+dy4[k]) || G.owner(x,y,k)<0) continue;
            C a{x*4,y*4},b{(x+dx4[k])*4,(y+dy4[k])*4},m{(a.x+b.x)/2,(a.y+b.y)/2};
            if(!G.crossing(x,y)) {links.push_back({a,m}); arc(a,m,1);}
            if(!G.crossing(x+dx4[k],y+dy4[k])) {links.push_back({m,b}); arc(m,b,1);}
        }
    }
    for(auto q:crossings) {arc({q.x-2,q.y},{q.x+2,q.y},1); arc({q.x,q.y-2},{q.x,q.y+2},1);}
    for(int n=0;n<nets;++n) for(const auto& l:fixedLinks[n]) {links.push_back(l); arc(l.a,l.b,1);}
    for(const auto& s:stubs) for(const auto& l:s.links) arc(l.a,l.b,1);
    std::vector<uint8_t> adapted(stubs.size(),0);
    std::unordered_map<uint64_t,uint8_t> phase;
    for(int n=0;n<nets;++n) {
        if(sinksOf[n].empty()) continue;
        std::vector<C> q{driverCell[n]}; phase[key(driverCell[n])]=0;
        for(size_t i=0;i<q.size();++i) {
            uint8_t v=phase[key(q[i])];
            auto it=graph.find(key(q[i])); if(it==graph.end()) continue;
            for(const auto& a:it->second) {
                uint8_t w=static_cast<uint8_t>(v^a.inv); auto [pt,fresh]=phase.emplace(key(a.to),w);
                if(fresh) q.push_back(a.to); else if(pt->second!=w) throw std::logic_error("Grid route forms an inconsistent cycle");
            }
        }
    }
    for(size_t si=0;si<stubs.size();++si) {
        auto it=phase.find(key(stubs[si].port)); if(it==phase.end()) throw std::logic_error("Grid sink not connected");
        adapted[si]=it->second!=static_cast<uint8_t>(stubs[si].inverted);
    }
    // Emit through ClassicPatch; finish() independently re-checks every clue.
    buildCheckpoint("Emit");
    ClassicPatch patch; size_t adapters=0;
    for(const auto& g:gates_) patch.nor(g.slot);
    for(const auto& l:links) patch.link(global(l.a),global(l.b),global({(l.a.x+l.b.x)/2,(l.a.y+l.b.y)/2}));
    for(size_t si=0;si<stubs.size();++si) {
        const auto& s=stubs[si];
        for(size_t i=0;i<s.links.size();++i) {
            const auto& l=s.links[i];
            if(i==s.adapter && adapted[si]) {patch.link(global(l.a),global(s.middle),global(s.first)); patch.link(global(s.middle),global(l.b),global(s.second)); ++adapters;}
            else patch.link(global(l.a),global(l.b),global({(l.a.x+l.b.x)/2,(l.a.y+l.b.y)/2}));
        }
    }
    for(auto q:crossings) {
        Coord at=global(q); auto p=[&](int ox,int oy){return Coord{at.x+ox,at.y+oy};};
        patch.count(at,1,{p(-1,-1),p(1,-1),p(-1,1),p(1,1)});
        patch.count(p(0,-1),1,{p(0,-2),p(-1,-1),p(1,-1)});
        patch.count(p(-1,0),1,{p(-2,0),p(-1,-1),p(-1,1)});
        patch.count(p(1,0),1,{p(2,0),p(1,-1),p(1,1)});
        patch.count(p(0,1),1,{p(0,2),p(-1,1),p(1,1)});
    }
    for(int n=0;n<nets;++n) patch.variable(drivers_[n].at);
    auto problem=patch.finishGrid(1);
    if(info) {
        info->width=problem.width; info->height=problem.height; info->gates=gates_.size(); info->nets=drivers_.size();
        info->links=links.size(); info->crossings=crossings.size(); info->adapters=adapters; info->retries=rips;
        info->routeMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }

    return problem;
}
}
